#pragma once
#include "../func.hpp"
#include "mir/optim/function_pass.hpp"

namespace foptim::fmir {

class BBReordering : public FunctionPass {
 public:
  void apply(MFunc& func, const conf::CompConf&) final override;
};

}  // namespace foptim::fmir
