#pragma once
#include "optim/function_pass.hpp"


namespace foptim::optim {

// + [ ] remove bb with no predecessor
// + [ ] merge bb with pred if thats only edge
// + [ ] eleminate bb args if only 1 pred
// + [ ] eliminate bb if only a single jump
// + [ ] convert if else into cmove
// + [ ] 

class SimplifyCFG final : public FunctionPass {
public:
  void apply(fir::Context &, fir::Function &func) override {

    ZoneScopedN("SimplifyCFG");
  }
};

} // namespace foptim::optim
