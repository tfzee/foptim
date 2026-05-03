#pragma once
#include "optim/module_pass.hpp"

namespace foptim::optim {

class VerifyModule final : public ModulePass {
 public:
  void apply(fir::Context &ctx, JobSheduler * /*unused*/) override {
    ASSERT(ctx->verify());
  }
};
}  // namespace foptim::optim
