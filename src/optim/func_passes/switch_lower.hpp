#pragma once
#include "ir/function.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

// Rewrites the switches that are worth a jump table into slot switches: a
// slot is computed from the value, checked against the default and switched
// on with the cases 0..n-1, an unreachable default and edges without block
// arguments. Large sparse switches get a compare tree over dense clusters.
class SwitchLower final : public FunctionPass {
 public:
  class Config {};

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
