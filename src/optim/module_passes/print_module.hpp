#pragma once
#include "ir/context.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/module_pass.hpp"

namespace foptim::optim {

class PrintModule final : public ModulePass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, JobSheduler * /*shed*/,
                          AnalysisManager & /*analyMan*/) override {
    if (ctx.config->debug.print_color) {
      fmt::println("{:cd}", ctx);
    } else {
      fmt::println("{:d}", ctx);
    }
    return PreservedAnalysis::all();
  }
};

}  // namespace foptim::optim
