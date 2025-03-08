#include "arg_parsing/parser.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "mir/func.hpp"
#include "mir/matcher.hpp"
#include "mir/optim/bb_reordering.hpp"
#include "mir/optim/calling_conv.hpp"
#include "mir/optim/copy_prop.hpp"
#include "mir/optim/dce.hpp"
#include "mir/optim/inst_simplify.hpp"
#include "mir/optim/legalization.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "mir/optim/register_joining.hpp"
#include "optim/analysis/attributer/KnownStackBits.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/func_passes/dce.hpp"
#include "optim/func_passes/inline.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/func_passes/licm.hpp"
#include "optim/func_passes/llvm_intrin_lowering.hpp"
#include "optim/func_passes/loop_rotate.hpp"
#include "optim/func_passes/loop_unroll.hpp"
#include "optim/func_passes/lvn.hpp"
#include "optim/func_passes/mem2reg.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/func_passes/simple_vectorize.hpp"
#include "optim/func_passes/simplify_cfg.hpp"
#include "optim/func_passes/stack_known_bits.hpp"
#include "optim/function_pass.hpp"
#include "optim/module_passes/IPCP.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include "utils/parameters.hpp"
#include "utils/todo.hpp"
#include "x86_codegen/backend.hpp"
#include "llvm/llir_loader.hpp"

#include <tracy/Tracy.hpp>

void parse_llvm_ir(foptim::fir::Context &ctx);
void optimize_fir(foptim::fir::Context &ctx);
void lower_to_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals);
void optimize_mir(foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals);
void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals);

int main(int argc, char *argv[]) {
  ZoneScopedN("BASE");

  parse_args(argc, argv);

  foptim::fir::Context ctx;
  {
    // fir
    parse_llvm_ir(ctx);
    optimize_fir(ctx);
  }
  {
    // mir
    foptim::FVec<foptim::fmir::MFunc> funcs;
    foptim::FVec<foptim::fmir::Global> globals;
    foptim::FVec<foptim::IRString> decls;
    for (auto &[decl, f] : ctx.data->storage.functions) {
      if (f.is_decl()) {
        decls.push_back(decl);
      }
    }
    lower_to_mir(ctx, funcs, globals);
    optimize_mir(funcs, globals);

    // asm
    codegen(funcs, decls, globals);
  }

  // cleanup
  ctx.free();
  foptim::utils::TempAlloc<void *>::free();
  foptim::utils::IRAlloc<void *>::free();
  return 0;
}

void parse_llvm_ir(foptim::fir::Context &ctx) {
  ZoneScopedN("LLIR LOADING");
  load_llvm_ir(foptim::utils::in_file_path.c_str(), ctx);
  foptim::utils::TempAlloc<void *>::reset();
  ASSERT(ctx->verify());
}

void optimize_fir(foptim::fir::Context &ctx) {
  ZoneScopedN("Optim FIR");
  using namespace foptim::optim;
  foptim::optim::StaticFunctionPassManager<Mem2Reg>{}.apply(ctx);
  ASSERT(ctx->verify());

  foptim::optim::StaticFunctionPassManager<InstSimplify, SimplifyCFG, DCE>{}
      .apply(ctx);
  fmt::print("================MID====================\n");
  for (const auto &[_, func] : ctx.data->storage.functions) {
    fmt::print("{:d}\n", func);
  }
  ctx->verify();
  foptim::optim::StaticFunctionPassManager<LVN, SCCP, InstSimplify, DCE>{}
      .apply(ctx);
  foptim::optim::StaticFunctionPassManager<SimplifyCFG>{}.apply(ctx);
  foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<StackKnownBits>{}.apply(ctx);
  ASSERT(ctx->verify());
  foptim::optim::StaticFunctionPassManager<Mem2Reg>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LLVMInstrinsicLowering>{}.apply(ctx);
  foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  ASSERT(ctx->verify());

  foptim::optim::StaticFunctionPassManager<InstSimplify>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LoopRotate>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LICM>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<Inline<>>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LVN, SCCP, InstSimplify, DCE>{}
      .apply(ctx);
  foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<SimplifyCFG, InstSimplify>{}.apply(
      ctx);
  foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<Unroll>{}.apply(ctx);
  // foptim::optim::StaticFunctionPassManager<SimpleVectorizer>{}.apply(ctx);
  ASSERT(ctx->verify());
  foptim::optim::StaticFunctionPassManager<SimplifyCFG>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LVN, SCCP, DCE>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<SimplifyCFG, InstSimplify>{}.apply(
      ctx);

  // ensure no constants math left
  foptim::optim::StaticFunctionPassManager<SCCP>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<DCE>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<InstSimplify>{}.apply(ctx);
  fmt::print("================FIR END====================\n");
  for (const auto &[_, func] : ctx.data->storage.functions) {
    fmt::print("{:d}\n", func);
  }
  ASSERT(ctx->verify());

  // {
  //   foptim::utils::print << "MEMREG JuST TESTING Attributor\n";
  //   AttributerManager manager;
  //   for (const auto &[_, func] : ctx.data->storage.functions) {
  //     for (auto bb : func.get_bbs()) {
  //       for (auto instr : bb->instructions) {
  //         if (instr->is(foptim::fir::InstrType::LoadInstr)) {
  //           manager.get_or_create_analysis<KnownStackBits>(
  //               foptim::fir::ValueR{instr});
  //         }
  //       }
  //     }
  //     manager.run();
  //     manager.materialize(ctx);
  //   }
  // }
}

void lower_to_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals) {
  ZoneScopedN("FIR to MIR lowering");
  funcs.reserve(ctx->storage.functions.size());
  globals.reserve(ctx->storage.storage_global.n_used());

  for (const auto *slab_g : ctx->storage.storage_global._slot_slab_starts) {
    for (size_t i = 0;
         i < decltype(ctx->storage.storage_global)::_slot_slab_len; i++) {
      const auto *v = &slab_g[i];
      if (v->used) {
        auto size = v->data.n_bytes;
        foptim::fmir::Global glob = {
            .name = v->data.name.c_str(), .data = {}, .reloc_info = {}};
        for (const auto &rel_inf : v->data.reloc_info) {
          ASSERT(rel_inf.ref->is_global());
          glob.reloc_info.push_back(foptim::fmir::Global::RelocationInfo{
              .offset = rel_inf.offset,
              .name = rel_inf.ref->as_global()->name.c_str()});
        }
        glob.data.resize(size, 0);
        memcpy(glob.data.data(), v->data.init_value, size);
        globals.push_back(glob);
      }
    }
  }

  auto matcher = foptim::fmir::GreedyMatcher{};
  fmt::print("================MATCHING====================\n");
  for (auto [_, func] : ctx->storage.functions) {
    if (func.is_decl()) {
      continue;
    }
    auto res = matcher.apply(func);
    funcs.push_back(std::move(res));
    foptim::utils::TempAlloc<void *>::reset();
  }
}

void optimize_mir(foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals) {
  (void)globals;
  ZoneScopedN("MIR Optim");
  fmt::print("================MIR OPTIM====================\n");
  for (auto &f : funcs) {
    fmt::println("{}", f);
  }
  // running dead to make inst simplify work better
  foptim::fmir::DeadCodeElim{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::LocalCopyPropagation{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::DeadCodeElim{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::CallingConv{}.first_stage(funcs);
  foptim::fmir::Legalizer{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::RegisterJoining{}.apply(funcs);
  fmt::print("================REGJOIN====================\n");
  for (auto &f : funcs) {
    fmt::println("{}", f);
  }
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::RegAlloc{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::CallingConv{}.second_stage(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::BBReordering{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  fmt::print("================MIR END====================\n");
  // TODO("okak");
}

void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals) {
  ZoneScopedN("Codegen");
  foptim::codegen::run(funcs, decls, globals);
  fmt::println("Done!");
}
