#include "fir_pipeline.hpp"

#include <fmt/base.h>
#include <fmt/core.h>
#include <unistd.h>

#include <deque>
#include <ranges>

#include "arg_parsing/parser.hpp"
#include "config/compiler_config.hpp"
#include "config/compiler_passes.hpp"
#include "ir/context.hpp"
#include "ir/helpers.hpp"
#include "llvm/llir_loader.hpp"
#include "optim/function_pass.hpp"
#include "optim/module_pass.hpp"
#include "utils/job_system.hpp"
#include "utils/todo.hpp"
#include "utils/tracy.hpp"

namespace foptim::conf::pipeline {

void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed) {
  ZoneScopedN("Optim FIR");
  using namespace foptim;
  using namespace foptim::optim;
  std::vector<PassConfig *> passes_worklist;
  std::deque<PipelineElem> pipeline_worklist;

  pipeline_worklist.push_back(PipelineElem{ctx.config->optim.fir_pipeline});

  // construct full pipeline
  while (!pipeline_worklist.empty()) {
    auto curr_pipe = pipeline_worklist.back();
    pipeline_worklist.pop_back();
    if (curr_pipe.type == PipelineElem::Pipeline) {
      for (auto elem : std::ranges::reverse_view(curr_pipe.pipeline->passes)) {
        pipeline_worklist.push_back(elem);
      }
    } else {
      passes_worklist.push_back(*curr_pipe.pass.get_raw_ptr());
    }
  }

  // reduce when bisecting
  const auto n_actual_run =
      ctx.config->debug.bisect != 0
          ? std::min((u64)ctx.config->debug.bisect, passes_worklist.size())
          : passes_worklist.size();
  fmt::println("Having {} passes and running {} passes", passes_worklist.size(),
               n_actual_run);
  size_t curr_pass = 0;
  conf::PrintFuncConf print_debug_func{};
  conf::VerifyFuncConf verify_debug_func{};
  while (curr_pass < n_actual_run) {
    auto *pass = passes_worklist[curr_pass];
    curr_pass++;
    switch (pass->pass_type()) {
        // merge function passes so we can run them in parralel
      case PassConfig::FIR_Function: {
        foptim::optim::ParallelFunctionPassManager man{};
        man.push_pass(pass);
        if (ctx.config->debug.print_between_passes) {
          man.push_pass(&print_debug_func);
        }
        if (ctx.config->debug.verify_between_passes) {
          man.push_pass(&verify_debug_func);
        }
        while (curr_pass < n_actual_run &&
               passes_worklist[curr_pass]->pass_type() ==
                   PassConfig::PassType::FIR_Function) {
          man.push_pass(passes_worklist[curr_pass]);
          if (ctx.config->debug.print_between_passes) {
            man.push_pass(&print_debug_func);
          }
          if (ctx.config->debug.verify_between_passes) {
            man.push_pass(&verify_debug_func);
          }
          curr_pass++;
        }
        man.apply(ctx, shed);
        break;
      }
      case PassConfig::FIR_Module: {
        foptim::optim::ModulePassManager man{};
        man.push_pass(pass->_construct_module_pass());
        while (curr_pass < n_actual_run &&
               passes_worklist[curr_pass]->pass_type() ==
                   PassConfig::PassType::FIR_Module) {
          man.push_pass(passes_worklist[curr_pass]->_construct_module_pass());
          curr_pass++;
        }
        man.apply(ctx, shed);
        break;
      }
      default: {
        fmt::println("Cant run the pass '{}' in the FIR pipeline",
                     pass->get_name());
        ASSERT(false);
      }
    }
  }

  ASSERT(ctx->verify());
}




// [[maybe_unused]] void optimize_fir(foptim::fir::Context &ctx,
//                                    foptim::JobSheduler *shed) {
//   (void)shed;
//   ZoneScopedN("Optim FIR");
//   using namespace foptim::optim;
//   if (foptim::utils::verbosity > 0) {
//     fmt::print("================FIR====================\n");
//     fmt::print("================FIR START====================\n");
//   }
//   ASSERT(ctx->verify());
//   foptim::optim::StaticParallelFunctionPassManager<DCE>{}.apply(ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<
//       LegalizeStructs, LLVMInstrinsicLowering, SORA, Mem2Reg, DoubleLoadElim,
//       DCE, IntrinSimplify, InstSimplify, DCE, SimplifyCFG, LVN, DCE>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticModulePassManager<FuncPropAnnotator, GlobalPromotion,
//                                          ArgPromotion, GDCE>{}
//       .apply(ctx, shed);

//   foptim::optim::StaticParallelFunctionPassManager<
//       DCE, CmpKnownValProp, SimplifyCFG, TailRecElim, LICM, LoopRotate,
//       LoopSimplify, DCE, LVN, SCCP, IntrinSimplify, InstSimplify,
//       DoubleLoadElim, DCE, SimplifyCFG, StackKnownBits, SORA, Mem2Reg,
//       SimplifyCFG, DCE, LVN, InstSimplify, ConstLoopEval, LoopSimplify,
//       InstSimplify, SimplifyCFG>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticModulePassManager<
//       FuncPropAnnotator, IPCP, GlobalPromotion, Inline<>, Inline<>, Inline<>,
//       ArgPromotion, GDCE, FunctionDeDup<true>, GDCE, FuncPropAnnotator>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<
//       SimplifyCFG, CmpKnownValProp, InstSimplify, SimplifyCFG, LICM, DCE,
//       LoopSimplify, LoopUnswitch, LoopUnroll, SimplifyCFG, DCE, SLPVectorizer,
//       LVN, SCCP, IntrinSimplify, InstSimplify, ConstLoopEval, InstSimplify,
//       CmpKnownValProp, SimplifyCFG, DCE>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticModulePassManager<
//       FuncPropAnnotator, IPCP, GlobalPromotion, Inline<>, Inline<>, Inline<>,
//       ArgPromotion, FunctionDeDup<true>, GDCE, FuncPropAnnotator>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<
//       SimplifyCFG, InstSimplify, SimplifyCFG, TailRecElim, SimplifyCFG, DCE,
//       LoopSimplify, LoopUnswitch, IntrinSimplify, InstSimplify, DCE>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<StackKnownBits, SORA,
//                                                    Mem2Reg, SimplifyCFG, DCE>{}
//       .apply(ctx, shed);

//   foptim::optim::StaticModulePassManager<
//       ArgPromotion, FuncPropAnnotator, FunctionDeDup<false>, GDCE, IPCP,
//       GlobalPromotion, Inline<>, Inline<>, GDCE, FuncPropAnnotator>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<
//       LVN, SCCP, DoubleLoadElim, DCE, IntrinSimplify, InstSimplify,
//       CmpKnownValProp, SimplifyCFG, SCCP>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticModulePassManager<
//       ArgPromotion, FuncPropAnnotator, FunctionDeDup<false>, Inline<>, Inline<>,
//       Inline<>, GDCE, IPCP, FuncPropAnnotator>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<
//       SimplifyCFG, LVN, SCCP, DoubleLoadElim, DCE, IntrinSimplify, SimplifyCFG,
//       InstSimplify, SCCP, DCE, InstSimplify, ConstLoopEval, LoopSimplify,
//       LoopUnswitch, LoopUnroll, SimplifyCFG, DCE, SLPVectorizer, InstSimplify,
//       SimplifyCFG>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticModulePassManager<
//       ArgPromotion, FuncPropAnnotator, FunctionDeDup<false>, Inline<>, Inline<>,
//       GDCE, IPCP, FuncPropAnnotator>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<
//       SimplifyCFG, LVN, SCCP, DCE, IntrinSimplify, InstSimplify, DCE,
//       SLPVectorizer, InstSimplify, SimplifyCFG>{}
//       .apply(ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<
//       LegalizeVecs, SCCP, LVN, InstSimplify, DCE, LVN, InstSimplify, DCE>{}
//       .apply(ctx, shed);
//   // for (const auto &[_, func] : ctx->storage.functions) {
//   //   fmt::println("{:cd}", *func);
//   // }
//   // general cleanup / legalization / finalization
//   foptim::optim::StaticParallelFunctionPassManager<MergeAllocaPass>{}.apply(
//       ctx, shed);
//   foptim::optim::StaticParallelFunctionPassManager<
//       LVN, InstSimplify, SCCP, DCE, LVN, InstSimplify, SimplifyCFG, DCE,
//       LegalizeVecs, InstSimplify, SCCP, LegalizeVecs, DCE>{}
//       .apply(ctx, shed);

//   ASSERT(ctx->verify());
//   // {
//   //   auto *slab = ctx->storage.storage_global._slot_start.load();
//   //   while (slab != nullptr) {
//   //     for (auto &i : slab->data) {
//   //       const auto *v = &i;
//   //       if (v->used == foptim::utils::SlotState::Used) {
//   //         // auto size = v->data->n_bytes;
//   //         // foptim::fmir::Global glob = {.name = v->data->name.c_str(),
//   //         //                              .data = {},
//   //         //                              .size = 0,
//   //         //                              .reloc_info = {},
//   //         //                              .vis = v->data->linkvis};
//   //         fmt::println("{:cd}", *v->data);
//   //       }
//   //     }
//   //     slab = slab->next;
//   //   }
//   // }
//   if (foptim::utils::verbosity > 0) {
//     fmt::print("================FIR END====================\n");
//   }
// }

}  // namespace foptim::conf::pipeline
