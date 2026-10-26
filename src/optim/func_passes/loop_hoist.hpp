#pragma once
#include "optim/function_pass.hpp"

namespace foptim::optim {

// inner loop that only reads memory of da outer loop does not change,
// and only depends on values from before the outer loop can be moved in front of the outer
// loop. This is something gcc gig afails it it seems
class LoopHoist final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
