#include "ir/context.hpp"
#include "mir/func.hpp"
#include "mir/matcher.hpp"
#include "mir/optim/bb_reordering.hpp"
#include "mir/optim/inst_simplify.hpp"
#include "mir/optim/invoke_lower.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "optim/func_passes/clean.hpp"
#include "optim/func_passes/dce.hpp"
#include "optim/func_passes/gvn.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/func_passes/llvm_intrin_lowering.hpp"
#include "optim/func_passes/loop_rotate.hpp"
#include "optim/func_passes/mem2reg.hpp"
#include "optim/func_passes/pre.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/function_pass.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include "utils/parameters.hpp"
#include "utils/todo.hpp"
#include "x86_codegen/backend.hpp"
#include "llvm/llir_loader.hpp"

#include <Tracy/tracy/Tracy.hpp>

using foptim::utils::Debug;

void parse_args(int argc, char *argv[]) {
  ASSERT_M(argc == 3,
           "Takes exactly 2 argument the .ll file and the out .ss file");
  foptim::utils::in_file_path = argv[1];
  foptim::utils::out_file_path = argv[2];
}

void parse_llvm_ir(foptim::fir::Context &ctx) {
  ZoneScopedN("LLIR LOADING");
  load_llvm_ir(foptim::utils::in_file_path, ctx);
  foptim::utils::TempAlloc<void *>::reset();
  ASSERT(ctx->verify());
}

void optimize_fir(foptim::fir::Context &ctx) {
  ZoneScopedN("Optim FIR");
  using namespace foptim::optim;
  foptim::optim::StaticFunctionPassManager<Mem2Reg>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<InstSimplify>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LVN>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<EPathPRE>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<SCCP>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<DCE>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LLVMInstrinsicLowering>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<LoopRotate>{}.apply(ctx);
  foptim::optim::StaticFunctionPassManager<InstSimplify>{}.apply(ctx);
  // foptim::optim::StaticFunctionPassManager<Clean>{}.apply(ctx);

  // foptim::optim::StaticFunctionPassManager<
  //     InstSimplify, LVN, EPathPRE, SCCP, DCE, InstSimplify,
  //     InstSimplify, Clean>{}
  //     .apply(ctx);
  ASSERT(ctx->verify());
}

void lower_to_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals) {
  funcs.reserve(ctx->storage.functions.size());
  globals.reserve(ctx->storage.storage_global.n_used());

  ZoneScopedN("Conversion");
  for (const auto *slab_g : ctx->storage.storage_global._slot_slab_starts) {
    for (size_t i = 0;
         i < decltype(ctx->storage.storage_global)::_slot_slab_len; i++) {
      const auto *v = &slab_g[i];
      if (v->used) {
        auto size = v->data.n_bytes;
        auto name = "G_" + std::to_string((foptim::u64) & (v->data));
        foptim::fmir::Global glob = {.name = name, .data = {}};
        glob.data.resize(size, 0);
        globals.push_back(glob);
      }
    }
  }

  auto matcher = foptim::fmir::GreedyMatcher{};
  for (auto [_, func] : ctx->storage.functions) {
    auto res = matcher.apply(func);
    // Debug << res;
    funcs.push_back(std::move(res));
    foptim::utils::TempAlloc<void *>::reset();
  }
}

void optimize_mir(foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals) {
  (void)globals;
  foptim::fmir::RegAlloc{}.apply(funcs);
  foptim::fmir::InvokeLower{}.apply(funcs);
  foptim::fmir::InstSimplify{}.apply(funcs);
  foptim::fmir::BBReordering{}.apply(funcs);
}

void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::fmir::Global> &globals) {

  ZoneScopedN("Codegen");
  foptim::codegen::run(funcs, globals);
}

int main(int argc, char *argv[]) {
  ZoneScopedN("BASE");

  parse_args(argc, argv);

  // fir
  foptim::fir::Context ctx;
  parse_llvm_ir(ctx);
  optimize_fir(ctx);

  // mir
  foptim::FVec<foptim::fmir::MFunc> funcs;
  foptim::FVec<foptim::fmir::Global> globals;
  lower_to_mir(ctx, funcs, globals);
  optimize_mir(funcs, globals);

  // asm
  codegen(funcs, globals);

  // cleanup
  ctx.free();
  foptim::utils::TempAlloc<void *>::free();
  foptim::utils::IRAlloc<void *>::free();
  return 0;
}
