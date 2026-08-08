#pragma once
#include "ir/function.hpp"
#include "optim/analysis/AnalysisManager.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class SimpleVectorizer final : public FunctionPass {
public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};
} // namespace foptim::optim
