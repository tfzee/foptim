#pragma once
#include "../func.hpp"
#include "function_pass.hpp"

namespace foptim::fmir {

class DeadCodeElim : public FunctionPass {
  void apply_impl(MFunc &func);

 public:
  void apply(MFunc &func, const conf::CompConf &config) final override {
    (void)config;
    apply_impl(func);
  }
};

}  // namespace foptim::fmir
