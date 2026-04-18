#pragma once
#include "../function_pass.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/loop_analysis.hpp"

namespace foptim::optim {

class LoopRotate final : public FunctionPass {
 public:
  void apply(fir::Context &ctx, fir::Function &func);

  bool apply(fir::Context &ctx, const CFG &cfg, LoopInfo &linfo);
};
}  // namespace foptim::optim
