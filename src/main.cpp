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
#include "optim/func_passes/loop_rotate.hpp"
#include "optim/func_passes/mem2reg.hpp"
#include "optim/func_passes/pre.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/function_pass.hpp"
#include "utils/logging.hpp"
#include "utils/parameters.hpp"
#include "utils/todo.hpp"
#include "x86_codegen/backend.hpp"
#include "llvm/llir_loader.hpp"

#include <Tracy/tracy/Tracy.hpp>

using foptim::utils::Debug;

int main(int argc, char *argv[]) {
  FrameMark;
  ZoneScopedN("BASE");
  foptim::fir::Context ctx;

  ASSERT_M(argc == 3,
           "Takes exactly 2 argument the .ll file and the out .ss file");
  {
    ZoneScopedN("LLIR LOADING");
    load_llvm_ir(argv[1], ctx);
    foptim::utils::out_file_path = argv[2];
  }
  {
    ZoneScopedN("FIR");

    ASSERT(ctx->verify());

    for (auto [_, func] : ctx->storage.functions) {
      Debug << func << "\n";
    }
    Debug << "\n\n";

    {
      ZoneScopedN("Optim");

      using namespace foptim::optim;
      foptim::optim::StaticFunctionPassManager<Mem2Reg>{}.apply(ctx);
      foptim::optim::StaticFunctionPassManager<
          InstSimplify, LVN, EPathPRE, SCCP, DCE, InstSimplify, LoopRotate,
          InstSimplify, Clean>{}
          .apply(ctx);
    }

    for (auto [_, func] : ctx->storage.functions) {
      Debug << func;
    }
    ASSERT(ctx->verify());
  }

  // return 0;

  foptim::FVec<foptim::fmir::MFunc> funcs;
  foptim::FVec<foptim::fmir::Global> globals;
  funcs.reserve(ctx->storage.functions.size());
  globals.reserve(ctx->storage.storage_global.n_used());

  {
    ZoneScopedN("FMIR");
    {
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

      for (auto [_, func] : ctx->storage.functions) {
        auto res = foptim::fmir::GreedyMatcher{}.apply(func);
        Debug << res;
        funcs.push_back(std::move(res));
      }
    }

    {
      ZoneScopedN("Optim2");
      foptim::fmir::RegAlloc{}.apply(funcs);
      foptim::fmir::InvokeLower{}.apply(funcs);
      foptim::fmir::InstSimplify{}.apply(funcs);
      foptim::fmir::BBReordering{}.apply(funcs);
    }

    Debug << "\n";
    for (auto func : funcs) {
      Debug << func;
    }

    {
      ZoneScopedN("Codegen");
      foptim::codegen::run(funcs, globals);
    }
  }

  // ctx->print_stats();
  FrameMark;
  return 0;
}
