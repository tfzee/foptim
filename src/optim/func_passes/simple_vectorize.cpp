#include "simple_vectorize.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"

namespace foptim::optim {

bool apply_it(CFG &cfg, LoopInfo &loop, fir::Context &ctx,
              fir::Function &func) {
  (void)loop;
  (void)ctx;
  (void)func;
  LoopRangeAnalysis range;
  if (range.update(cfg, loop)) {
    range.dump();
  }
  return false;
}

void SimpleVectorizer::apply(fir::Context &ctx, fir::Function &func) {
  (void)ctx;
  (void)func;

  CFG cfg{func};
  Dominators dom{cfg};
  LoopInfoAnalysis linfo{dom};
  fmt::println("{}", func);

  for (auto &loop : linfo.info) {
    if (apply_it(cfg, loop, ctx, func)) {
      TODO("okka");
    }
  }
}

} // namespace foptim::optim
