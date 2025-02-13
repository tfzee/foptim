#include "arg_parsing/parser.hpp"
#include "ir/context.hpp"
#include "mir/func.hpp"
#include "mir/matcher.hpp"
#include "mir/optim/bb_reordering.hpp"
#include "mir/optim/calling_conv.hpp"
#include "mir/optim/dce.hpp"
#include "mir/optim/inst_simplify.hpp"
#include "mir/optim/legalization.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "mir/optim/register_joining.hpp"
#include "optim/func_passes/dce.hpp"
#include "optim/func_passes/inline.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/func_passes/licm.hpp"
#include "optim/func_passes/llvm_intrin_lowering.hpp"
#include "optim/func_passes/loop_rotate.hpp"
#include "optim/func_passes/lvn.hpp"
#include "optim/func_passes/mem2reg.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/func_passes/simplify_cfg.hpp"
#include "optim/function_pass.hpp"
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
  foptim::utils::Debug << "================INIT====================\n";
  for (const auto &[_, func] : ctx.data->storage.functions) {
    foptim::utils::Debug << func << "\n";
  }
  ASSERT(ctx->verify());

}

void optimize_fir(foptim::fir::Context &ctx) {
  ZoneScopedN("Optim FIR");
  using namespace foptim::optim;
  foptim::optim::StaticFunctionPassManager<Mem2Reg>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<DCE>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<InstSimplify>{}.apply(ctx);
  foptim::utils::Debug << "================OPTIMMIDDLE====================\n";
  for (const auto &[_, func] : ctx.data->storage.functions) {
    foptim::utils::Debug << func << "\n";
  }
  foptim::optim::StaticFunctionPassManager<LVN>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<SCCP>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<DCE>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<SimplifyCFG>{}.apply(ctx);
  ASSERT(ctx->verify());

  foptim::optim::StaticFunctionPassManager<LLVMInstrinsicLowering>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LoopRotate>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LICM>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<Inline<>>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<SimplifyCFG>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<InstSimplify>{}.apply(ctx);
  ASSERT(ctx->verify());

  foptim::optim::StaticFunctionPassManager<LVN>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<SCCP>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<DCE>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<InstSimplify>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<SimplifyCFG>{}.apply(ctx);
  ASSERT(ctx->verify());

  // ensure no constants math left
  foptim::optim::StaticFunctionPassManager<SCCP>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<DCE>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<InstSimplify>{}.apply(ctx);

  foptim::utils::Debug << "================OPTIMEND====================\n";
  for (const auto &[_, func] : ctx.data->storage.functions) {
    foptim::utils::Debug << func << "\n";
  }
  ASSERT(ctx->verify());
  TODO("okak");
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
        auto name = "G_" + std::to_string((foptim::u64) & (v->data));
        foptim::fmir::Global glob = {.name = name.c_str(), .data = {}};
        glob.data.resize(size, 0);
        memcpy(glob.data.data(), v->data.init_value, size);
        globals.push_back(glob);
      }
    }
  }

  auto matcher = foptim::fmir::GreedyMatcher{};
  foptim::utils::Debug << "================MATCHER====================\n";
  for (auto [_, func] : ctx->storage.functions) {
    if (func.is_decl()) {
      continue;
    }
    auto res = matcher.apply(func);
    funcs.push_back(std::move(res));
    // foptim::utils::Debug << funcs.back();
    foptim::utils::TempAlloc<void *>::reset();
  }
}

void optimize_mir(foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals) {
  (void)globals;

  ZoneScopedN("MIR Optim");
  foptim::fmir::DeadCodeElim{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::CallingConv{}.first_stage(funcs);
  foptim::fmir::Legalizer{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  // foptim::utils::Debug << "================MIROPTIM====================\n";
  // for (auto &func : funcs) {
  //   foptim::utils::Debug << func << "\n";
  // }
  foptim::fmir::RegisterJoining{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::RegAlloc{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  // foptim::utils::Debug << "================MIROPTIM====================\n";
  // for (auto &func : funcs) {
  //   foptim::utils::Debug << func << "\n";
  // }
  foptim::fmir::CallingConv{}.second_stage(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::BBReordering{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
}

void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals) {
  ZoneScopedN("Codegen");
  foptim::codegen::run(funcs, decls, globals);
  foptim::utils::Debug << "Done!";
}
