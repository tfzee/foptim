#pragma once
#include "../func.hpp"

namespace foptim::fmir {

class FunctionPass {
public:
  virtual void apply(MFunc &func, const conf::CompConf &config) {
    (void)config;
    (void)func;
    TODO("IMPL");
  }
};

} // namespace foptim::fmir
