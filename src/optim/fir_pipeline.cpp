#include "fir_pipeline.hpp"

#include <fmt/base.h>
#include <fmt/core.h>
#include <unistd.h>

#include <deque>

#include "config/compiler_config.hpp"
#include "config/compiler_passes.hpp"
#include "arg_parsing/parser.hpp"
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
  std::deque<utils::SRef<Pipeline>> pipeline_worklist;

  pipeline_worklist.push_back(ctx.config->optim.pipeline);

  // construct full pipeline
  while (!pipeline_worklist.empty()) {
    auto curr_pipe = pipeline_worklist.front();
    pipeline_worklist.pop_front();
    for (auto elem : curr_pipe->fir_passes) {
      switch (elem.type) {
        case PipelineElem::Pipeline:
          pipeline_worklist.push_back(elem.pipeline);
          break;
        case PipelineElem::Pass:
          passes_worklist.push_back(*elem.pass.get_raw_ptr());
          break;
      }
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
      case PassConfig::Function: {
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
                   PassConfig::PassType::Function) {
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
      case PassConfig::Module: {
        foptim::optim::ModulePassManager man{};
        man.push_pass(pass->_construct_module_pass());
        while (curr_pass < n_actual_run &&
               passes_worklist[curr_pass]->pass_type() ==
                   PassConfig::PassType::Module) {
          man.push_pass(passes_worklist[curr_pass]->_construct_module_pass());
          curr_pass++;
        }
        man.apply(ctx, shed);
        break;
      }
    }
  }

  ASSERT(ctx->verify());
}

}  // namespace foptim::conf::pipeline
