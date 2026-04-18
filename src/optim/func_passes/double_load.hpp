#pragma once
#include "ir/context.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class DoubleLoadElim final : public FunctionPass {
 public:
  void apply(fir::Context &ctx, fir::Function &func);
};
}  // namespace foptim::optim
