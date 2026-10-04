#pragma once
#include "ir/context.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class DoubleLoadElim final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};
}  // namespace foptim::optim
