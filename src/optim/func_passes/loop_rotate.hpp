#pragma once
#include "../function_pass.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/loop_analysis.hpp"

namespace foptim::optim {

class LoopRotate final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;

  bool apply(fir::Context &ctx, const CFG &cfg, LoopInfo &linfo);
};
}  // namespace foptim::optim
