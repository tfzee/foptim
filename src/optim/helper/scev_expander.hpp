#pragma once
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "utils/map.hpp"

namespace foptim::optim {

// Turns loop invariant expressions of a ScalarEvo back into instructions. The
// instructions are inserted in front of `insert_before`, which has to be
// dominated by the definitions of all invariants the expressions use (for
// example the terminator of ScalarEvo::entry_block()). Every expression is
// only expanded once, repeated requests (and shared sub expressions) reuse the
// instructions.
class SCEVExpander {
  fir::Context &ctx;
  ScalarEvo &evo;
  fir::Builder builder;
  TMap<SCEVRef, fir::ValueR> cache;

public:
  SCEVExpander(fir::Context &ctx, ScalarEvo &evo, fir::Instr insert_before)
      : ctx(ctx), evo(evo), builder(insert_before) {}

  // r has to satisfy evo.is_expandable(r)
  fir::ValueR expand(SCEVRef r);
};

} // namespace foptim::optim
