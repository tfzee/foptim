#pragma once
#include "../func.hpp"
#include "config/compiler_config.hpp"
#include "mir/optim/function_pass.hpp"

namespace foptim::fmir {

class InstSimplifyImpl {
 public:
  void impl_apply(MFunc &funcs);
  // void apply(FVec<MFunc> &funcs);
  void impl_early(MFunc &funcs);
  // void early_apply(FVec<MFunc> &funcs);
};

class InstSimplifyEarly : public FunctionPass, InstSimplifyImpl {
 public:
  void apply(MFunc &func, const conf::CompConf &) final override {
    impl_early(func);
  }
};

class InstSimplify : public FunctionPass, InstSimplifyImpl {
 public:
  void apply(MFunc &func, const conf::CompConf &) final override {
    impl_apply(func);
  }
};

}  // namespace foptim::fmir
