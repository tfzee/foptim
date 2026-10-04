#pragma once
#include <memory>

#include "ir/function.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/constraint_analysis.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

// The analyses that are cached on a function. Bit flags so a pass can say
// which of them stay valid.
enum class AnalysisKind : u32 {
  CFG = 1U << 0,
  // needs the CFG, so it is dropped whenever the CFG is
  Dominators = 1U << 1,
  // needs the CFG and refers to instructions (their ValueRs), so any pass
  // that touches instructions has to drop it
  Constraints = 1U << 2,
};

struct PreservedAnalysis {
  u32 mask = 0;

  static PreservedAnalysis all() { return PreservedAnalysis{.mask = ~0U}; }
  static PreservedAnalysis none() { return PreservedAnalysis{.mask = 0}; }
  // The block structure (blocks and their edges) was not touched, but
  // instructions may have changed.
  static PreservedAnalysis cfg_only() {
    return PreservedAnalysis{
        .mask = static_cast<u32>(AnalysisKind::CFG) |
                static_cast<u32>(AnalysisKind::Dominators)};
  }
  [[nodiscard]] bool preserves(AnalysisKind k) const {
    return (mask & static_cast<u32>(k)) != 0;
  }
};

// Lives on the function (fir::Function::analysis_cache) so there is no
// function -> analysis map to keep in sync when functions are created or
// erased, and no sharing between the jobs of different functions.
struct FunctionAnalysisCache {
  std::unique_ptr<CFG> cfg;
  std::unique_ptr<Dominators> dom;
  std::unique_ptr<ConstraintAnalysis> constraints;
};

/*
Caches the analyses per function and drops them according to the
PreservedAnalysis a pass returns.

Thread safety: a function's cache is only touched by the job that currently
owns that function. Function passes run one job per function, module passes
must only call into the manager for functions they own in the current job (or
from their serial part). The manager has no shared state.

The returned references stay valid until the analysis is invalidated, i.e.
until the pass returns (or until the pass itself reassigns the object).
A pass that changes the IR in a way that makes a cached analysis wrong has to
return the matching PreservedAnalysis, in assert builds this is checked by
verify_preserved.

Not cached yet (still built by each user, candidates in this order):
 - LiveVariables, LoopInfoAnalysis (+ DominatorTree), InductionVarAnalysis
 - AliasAnalyis, AllocaEscape, AccessAnalysis
 - CallGraph (module level, not per function)
 - AttributerManager / KnownBits (instruction level, cheap to invalidate on
   every change but expensive to rebuild)
*/
class AnalysisManager {
public:
  static CFG &cfg(fir::Function &func);
  static Dominators &dom(fir::Function &func);
  static ConstraintAnalysis &constraints(fir::Function &func);

  // drop everything the pass did not preserve
  static void invalidate(fir::Function &func, PreservedAnalysis pres);
  // drop all cached analyses of every function (used after module passes)
  static void invalidate_all(fir::Context &ctx);

  // assert builds only: recompute what the pass claimed to preserve and
  // compare it to the cache
  static void verify_preserved(fir::Function &func, PreservedAnalysis pres);
};

} // namespace foptim::optim
