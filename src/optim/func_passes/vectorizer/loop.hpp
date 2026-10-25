#pragma once
#include <memory>

#include "optim/func_passes/vectorizer/plan.hpp"

namespace foptim::optim::vec {

u32 max_vector_bits(const Features &features);

// Innermost single block countable loops. A seed is one loop, planning picks
// the vector factor with the best benefit over all iterations.
std::unique_ptr<SeedProvider> make_loop_seeds();

// Values used behind a single block loop become exit block arguments, so the
// loop skeleton only has to route the next values. Returns true if the IR
// changed.
bool route_exit_uses(fir::Context &ctx, CFG &cfg, LoopInfo &loop);

// the blocks the skeleton adds
struct LoopBlocks {
  fir::BasicBlock pre;
  fir::BasicBlock vector;
  fir::BasicBlock middle;
};

// Rewrites the loop into a pre block, a vector loop and a middle block that
// falls back to the original loop as the remainder. The plan was costed for
// plan.vf.
LoopBlocks emit_loop(fir::Context &ctx, Plan &plan);

}  // namespace foptim::optim::vec
