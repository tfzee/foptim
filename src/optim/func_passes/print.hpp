#pragma once
#include "config/compiler_config.hpp"
#include "ir/function.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {
class PrintFunc final : public FunctionPass {
public:
  struct Config {
    FString name_match;
  };
  Config config;

  void apply(fir::Context &ctx, fir::Function &func) override {
    if (!config.name_match.empty() &&
        !func.getName().contains(config.name_match)) {
      return;
    }
    if (ctx.config->debug.print_color) {
      fmt::println("{:cd}", func);
    } else {
      fmt::println("{:d}", func);
    }
  }
};
} // namespace foptim::optim
