#pragma once
#include "optim/function_pass.hpp"

namespace foptim::optim {

class IntrinSimplify final : public FunctionPass {
 public:
  void apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
