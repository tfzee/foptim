#pragma once
#include "config/compiler_config.hpp"
#include "ir/function.hpp"
#include "optim/analysis/AnalysisManager.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {
class PrintFunc final : public FunctionPass {
public:
  struct Config {
    FString name_match;
  };
  Config config;

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    if (!config.name_match.empty() &&
        !func.getName().contains(config.name_match)) {
      return PreservedAnalysis::all();
    }
    if (ctx.config->debug.print_color) {
      fmt::println("{:cd}", func);
    } else {
      fmt::println("{:d}", func);
    }
    return PreservedAnalysis::all();
  }
};
} // namespace foptim::optim
