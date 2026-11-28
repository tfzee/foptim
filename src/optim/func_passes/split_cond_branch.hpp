#pragma once
#include "optim/function_pass.hpp"

namespace foptim::optim {

// A branch on the and / or of single use compares becomes a chain of two
// branches, so a>0 && b>0 is two compare and jump pairs instead of two
// materialized flags. Meant to run after the last SimplifyCFG, which merges
// such chains.
class SplitCondBranch final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
