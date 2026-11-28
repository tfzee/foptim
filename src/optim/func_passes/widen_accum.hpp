#pragma once
#include "optim/function_pass.hpp"

namespace foptim::optim {

// a narrow accumulator of a single block loop that only adds (or ors, xors,
// ands, multiplie) truncated wide values, like the byte sum of a rotate with
// a variable amount computed in int, is carried at the width of the wide
// values and truncated on the exit edges. only the low bits of this kinda chain
// depend on the low bits of its operands, so the result is the same, and the
// vectorizer can keep the lanes wide instead of truncating every iteration.
class WidenAccum final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
