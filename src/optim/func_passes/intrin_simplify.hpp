#pragma once
#include "optim/analysis/AnalysisManager.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class IntrinSimplify final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
