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
#include "optim/function_pass.hpp"
#include "optim/module_pass.hpp"
#include "utils/job_system.hpp"
#include "utils/todo.hpp"
#include "utils/tracy.hpp"
#include "llvm/llir_loader.hpp"

namespace foptim::optim::pipeline {

void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed) {
  using conf::PassConfig;
  ZoneScopedN("Optim FIR");
  // TODO: use my types
  std::vector<PassConfig *> passes_worklist;
  std::deque<conf::PipelineElem> pipeline_worklist;

  pipeline_worklist.push_back(
      conf::PipelineElem{ctx.config->optim.fir_pipeline});

  // construct full pipeline
  while (!pipeline_worklist.empty()) {
    auto curr_pipe = pipeline_worklist.back();
    pipeline_worklist.pop_back();
    if (curr_pipe.type == conf::PipelineElem::Pipeline) {
      for (auto elem : std::ranges::reverse_view(curr_pipe.pipeline->passes)) {
        pipeline_worklist.push_back(elem);
      }
    } else {
      passes_worklist.push_back(*curr_pipe.pass.get_raw_ptr());
    }
  }
  // TODO destruction of passes kinda iffy

  // reduce when bisecting
  const auto n_actual_run =
      ctx.config->debug.bisect != 0
          ? std::min(static_cast<u64>(ctx.config->debug.bisect),
                     passes_worklist.size())
          : passes_worklist.size();
  fmt::println("Having {} FIR passes and running {} passes",
               passes_worklist.size(), n_actual_run);
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

} // namespace foptim::optim::pipeline
