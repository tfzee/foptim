#pragma once
#include "ir/function.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

// A value that is a pure function of one 8 bit value (compares, selects,
// and / or / xor, add / sub / mul, shifts, extends, truncs over constants and
// that one value) is evaluated for all 256 inputs and replaced by a load from
// a constant table when that is cheaper than the expression. A branch on an
// or of equality checks of one value that got no table (a value of more than
// 8 bits, say) becomes a switch.
class LUTMatch final : public FunctionPass {
 public:
  class Config {};

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
