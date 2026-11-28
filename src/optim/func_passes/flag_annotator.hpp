#pragma once
#include "optim/function_pass.hpp"

namespace foptim::optim {

// sets poison flags that the loader could not know (the frontend does not emit
// them) but that follow from the control flow, so analyses like ScalarEvo can
// rely on them. only flags that hold in every execution are added.
class FlagAnnotator final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
