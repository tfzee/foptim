#pragma once
#include "optim/module_pass.hpp"

namespace foptim::optim {

// inter procedural constant propagation
// replace arguments insideo of functions with constant args
class IPCP final : public ModulePass {
 public:
  void apply(fir::Context &ctx, JobSheduler * /*unused*/) override;
};
}  // namespace foptim::optim
