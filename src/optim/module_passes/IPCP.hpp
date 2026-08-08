#pragma once
#include "optim/module_pass.hpp"

namespace foptim::optim {

// inter procedural constant propagation
// replace arguments insideo of functions with constant args
class IPCP final : public ModulePass {
public:
  PreservedAnalysis apply(fir::Context & /*unused*/, JobSheduler * /*shed*/,
                          AnalysisManager & /*analyMan*/) override;
};
} // namespace foptim::optim
