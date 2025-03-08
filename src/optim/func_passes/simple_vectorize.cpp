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
  if (!range.update(cfg, loop)) {
    return false;
  }
  if (!range.known_upper || !range.known_lower) {
    return false;
  }
  auto iteration_count = (range.upper_bound - range.lower_bound) / range.a;
  if (iteration_count <= 10 || iteration_count % 4 != 0) {
    return false;
  }
  ASSERT(range.type == LoopRangeAnalysis::IterationType::PlusA);

  //figure out which instructions would be part of the vectorization
  //that is all instructions that 

  range.dump();
  TODO("okak");
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
