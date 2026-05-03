
#pragma once
#include "ir/function.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {
class VerifyFunc final : public FunctionPass {
 public:
  void apply(fir::Context & /*ctx*/, fir::Function &func) override {
    ASSERT(func.verify());
  }
};
}  // namespace foptim::optim
