#pragma once
#include "ir/function.hpp"
#include "optim/function_pass.hpp"
#include "config/compiler_config.hpp"

namespace foptim::optim {
class PrintFunc final : public FunctionPass {
 public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    if (ctx.config->debug.print_color) {
      fmt::println("{:cd}", func);
    } else {
      fmt::println("{:d}", func);
    }
  }
};
}  // namespace foptim::optim
