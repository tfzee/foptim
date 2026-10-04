#pragma once
#include "../function_pass.hpp"
#include "ir/function.hpp"
#include "optim/analysis/analysis_manager.hpp"

namespace foptim::optim {

class Mem2Reg final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
