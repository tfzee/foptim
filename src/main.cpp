#include "arg_parsing/parser.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "mir/func.hpp"
#include "mir/matcher.hpp"
#include "mir/optim/bb_reordering.hpp"
#include "mir/optim/calling_conv.hpp"
#include "mir/optim/dce.hpp"
#include "mir/optim/inst_simplify.hpp"
#include "mir/optim/legalization.hpp"
#include "mir/optim/lifetime_shortening.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "mir/optim/register_joining.hpp"
#include "optim/analysis/attributer/KnownStackBits.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/func_passes/dce.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/func_passes/licm.hpp"
#include "optim/func_passes/llvm_intrin_lowering.hpp"
#include "optim/func_passes/loop_rotate.hpp"
#include "optim/func_passes/loop_unroll.hpp"
#include "optim/func_passes/lvn.hpp"
#include "optim/func_passes/mem2reg.hpp"
#include "optim/func_passes/print.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/func_passes/simple_vectorize.hpp"
#include "optim/func_passes/simplify_cfg.hpp"
#include "optim/func_passes/stack_known_bits.hpp"
#include "optim/function_pass.hpp"
#include "optim/module_passes/IPCP.hpp"
#include "optim/module_passes/inline.hpp"
#include "utils/arena.hpp"
#include "utils/job_system.hpp"
#include "utils/logging.hpp"
#include "utils/parameters.hpp"
#include "utils/stable_vec_slot.hpp"
#include "utils/todo.hpp"
#include "x86_codegen/backend.hpp"
#include "llvm/llir_loader.hpp"

#include <tracy/Tracy.hpp>
#include <unistd.h>

void parse_llvm_ir(foptim::fir::Context &ctx);
void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed);
void lower_to_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals);
void optimize_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals);
void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals);

foptim::JobSheduler shed;

int main(int argc, char *argv[]) {
  ZoneScopedN("BASE");

  parse_args(argc, argv);
  {

    foptim::fir::Context ctx;
    {
      foptim::JobSheduler shed;
      shed.init(3);
      // fir
      parse_llvm_ir(ctx);
      optimize_fir(ctx, &shed);
      // cleanup
      shed.deinit();
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
      optimize_mir(ctx, funcs, globals);

      // asm
      codegen(funcs, decls, globals);
    }

    ctx.free();
  }
  foptim::utils::TempAlloc<void *>::free();
  foptim::utils::IRAlloc<void *>::free();
  return 0;
}

void parse_llvm_ir(foptim::fir::Context &ctx) {
  ZoneScopedN("LLIR LOADING");
  load_llvm_ir(foptim::utils::in_file_path.c_str(), ctx);
  foptim::utils::TempAlloc<void *>::reset();
}

void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed) {
  (void)shed;
  ZoneScopedN("Optim FIR");
  using namespace foptim::optim;
  ASSERT(ctx->verify());
  fmt::print("================FIR====================\n");
  for (const auto &[_, func] : ctx.data->storage.functions) {
    fmt::print("{:d}\n", func);
  }
  // foptim::optim::StaticFunctionPassManager<
  //     Mem2Reg, InstSimplify, DCE, LoopRotate, SimplifyCFG, LICM, DCE, LVN,
  //     SCCP, InstSimplify, DCE, SimplifyCFG>{} .apply(ctx);
  // foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  // foptim::optim::StaticFunctionPassManager<
  //     SimplifyCFG, DCE, StackKnownBits, Mem2Reg, InstSimplify, SimplifyCFG,
  //     LLVMInstrinsicLowering>{}
  //     .apply(ctx);
  // foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  // ASSERT(ctx->verify());
  // foptim::optim::StaticFunctionPassManager<
  //     Mem2Reg, InstSimplify, DCE, LoopRotate, SimplifyCFG, LICM, DCE, LVN,
  //     SCCP, InstSimplify, DCE, SimplifyCFG>{} .apply(ctx);
  // foptim::optim::StaticModulePassManager<IPCP, Inline<>>{}.apply(ctx);
  // foptim::optim::StaticFunctionPassManager<
  //     SimplifyCFG, DCE, StackKnownBits, Mem2Reg, LLVMInstrinsicLowering>{}
  //     .apply(ctx);
  // foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  // foptim::optim::StaticFunctionPassManager<
  //     LVN, SCCP, DCE, SimplifyCFG, InstSimplify, SCCP, DCE, InstSimplify,
  //     InstSimplify, SimplifyCFG, InstSimplify>{}
  //     .apply(ctx);
  foptim::optim::StaticParallelFunctionPassManager<Mem2Reg, InstSimplify, DCE>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      LoopRotate, SimplifyCFG, LICM, DCE, LVN, SCCP, InstSimplify, DCE,
      SimplifyCFG>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  foptim::optim::StaticParallelFunctionPassManager<
      SimplifyCFG, DCE, StackKnownBits, Mem2Reg, InstSimplify, SimplifyCFG,
      LLVMInstrinsicLowering>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  ASSERT(ctx->verify());
  foptim::optim::StaticParallelFunctionPassManager<
      Mem2Reg, InstSimplify, DCE, LoopRotate, SimplifyCFG, LICM, DCE>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<LVN, SCCP, InstSimplify, DCE,
                                                   SimplifyCFG>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<IPCP, Inline<>>{}.apply(ctx);
  foptim::optim::StaticParallelFunctionPassManager<
      SimplifyCFG, DCE, StackKnownBits, Mem2Reg, LLVMInstrinsicLowering>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<IPCP>{}.apply(ctx);
  foptim::optim::StaticParallelFunctionPassManager<
      LVN, SCCP, DCE, SimplifyCFG, InstSimplify, SCCP, DCE, InstSimplify,
      InstSimplify, SimplifyCFG, InstSimplify>{}
      .apply(ctx, shed);
  fmt::print("================END FIR====================\n");
  for (const auto &[_, func] : ctx.data->storage.functions) {
    fmt::print("{:d}\n", func);
  }
  ASSERT(ctx->verify());
  ctx.data->print_stats();
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
      if (v->used == foptim::utils::SlotState::Used) {
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

void optimize_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals) {
  (void)globals;
  ZoneScopedN("MIR Optim");
  // running dead to make inst simplify work better
  foptim::fmir::DeadCodeElim{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  ctx.data->storage.storage_instr.collect_garbage();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::LifetimeShortening{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::DeadCodeElim{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  ctx.data->storage.storage_instr.collect_garbage();
  foptim::fmir::CallingConv{}.first_stage(funcs);
  foptim::fmir::Legalizer{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::RegisterJoining{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  exit(1);
  foptim::fmir::RegAlloc{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::CallingConv{}.second_stage(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::BBReordering{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  fmt::print("================MIR END====================\n");
  for (auto &f : funcs) {
    fmt::println("{}", f);
  }
}

void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals) {
  ZoneScopedN("Codegen");
  foptim::codegen::run(funcs, decls, globals);
  fmt::println("Done!");
}
