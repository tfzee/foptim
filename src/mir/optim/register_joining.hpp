#pragma once
#include "config/compiler_config.hpp"
#include "mir/optim/function_pass.hpp"

namespace foptim::fmir {
class MFunc;

class RegisterJoining: public FunctionPass {
  void apply_impl(MFunc &func);

 public:
  void apply(MFunc &func, const conf::CompConf&);
};
}  // namespace foptim::fmir
