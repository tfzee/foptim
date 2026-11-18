#pragma once
#include "mir/func.hpp"
#include "mir/optim/function_pass.hpp"

namespace foptim::fmir {

// spill slots that are never live at the same time share one stack location,
// a smaller slot goes to the start of a bigger one
class StackSlotCoalesce : public FunctionPass {
 public:
  void apply(MFunc &func, const conf::CompConf &config) final;
};

}  // namespace foptim::fmir
