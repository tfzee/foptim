#pragma once
#include "optim/function_pass.hpp"

namespace foptim::optim {

// computes for every integer value how many low bits its users look at
// (like da llvm demanded bits) and rebuilds values whose users only need
// the low k bits at width k. works through loop carried cycles (block
// arguments) removes the sext / zext / trunc that were only there to change the
// width and leaves a part alone when it needs more new instructions than
// it removes.
class NarrowInts final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
