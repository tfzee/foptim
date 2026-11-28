#pragma once
#include "optim/function_pass.hpp"

namespace foptim::optim {

// The sign extension of a narrow counter that ScalarEvo knows does not wrap
// (the array index of a loop over an int) is an add recurrence of the wide
// type. It is carried as a header argument of the loop instead, so the
// extension disappears from the loop. Signed and equality compares of such a
// counter against a loop invariant value are done on the wide counter too,
// which lets the narrow counter die. Which then llows a bit clenaer codegen
// when loopnest gets enabled
class WidenIV final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
