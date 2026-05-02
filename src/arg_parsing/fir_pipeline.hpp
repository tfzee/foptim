
#include <fmt/base.h>
#include <fmt/core.h>
#include <unistd.h>

#include <deque>

#include "arg_parsing/compiler_config.hpp"
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

inline void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed) {
  ZoneScopedN("Optim FIR");
  using namespace foptim;
  using namespace foptim::optim;
  std::vector<PassConfig *> passes_worklist;
  std::deque<utils::SRef<Pipeline>> pipeline_worklist;

  pipeline_worklist.push_back(ctx.config->optim.pipeline);

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

  fmt::println("Running {} Passes", passes_worklist.size());
  size_t curr_pass = 0;
  size_t n_pass = passes_worklist.size();
  while (curr_pass < n_pass) {
    auto *pass = passes_worklist[curr_pass];
    // fmt::println("Constructing at {}", curr_pass);
    curr_pass++;
    switch (pass->pass_type()) {
      case PassConfig::Function: {
        foptim::optim::ParallelFunctionPassManager man{};
        man.push_pass(pass->_construct_function_pass());
        while (curr_pass < n_pass && passes_worklist[curr_pass]->pass_type() ==
                                         PassConfig::PassType::Function) {
          man.push_pass(passes_worklist[curr_pass]->_construct_function_pass());
          curr_pass++;
        }
        // fmt::println("Func end at {}", curr_pass);
        man.apply(ctx, shed);
        break;
      }
      case PassConfig::Module: {
        foptim::optim::ModulePassManager man{};
        man.push_pass(pass->_construct_module_pass());
        while (curr_pass < n_pass && passes_worklist[curr_pass]->pass_type() ==
                                         PassConfig::PassType::Module) {
          man.push_pass(passes_worklist[curr_pass]->_construct_module_pass());
          curr_pass++;
        }
        // fmt::println("Mod end at {}", curr_pass);
        man.apply(ctx, shed);
        break;
      }
    }
  }

  ASSERT(ctx->verify());
}

}  // namespace foptim::conf::pipeline
