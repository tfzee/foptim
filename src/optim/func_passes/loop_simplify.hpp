#pragma once
#include "ir/function.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {

class LoopSimplify final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    (void)ctx;
    CFG cfg{func};
    Dominators dom{cfg};
    LoopInfoAnalysis loops{dom};

    for (auto loop : loops.info) {
      fmt::println("{}", func);
      InductionVarAnalysis induct{cfg, loop};
      induct.dump();
    }
  }
};
} // namespace foptim::optim
