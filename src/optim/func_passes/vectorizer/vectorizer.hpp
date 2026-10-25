#pragma once
#include "optim/func_passes/vectorizer/plan.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

// Straight line code (SLP), loops and reductions through one planner: a seed
// provider finds the places that might be vectorized, the plan of each seed is
// read only and costed, the selector picks the plans (a loop plan against the
// straight line plans of its body) and only then the IR is rewritten.
class Vectorizer final : public FunctionPass {
 public:
  using Config = vec::Config;
  Config config;
  static constexpr bool debug_print = false;
  // whole function vectorization of a call clones the body of the callee
  static constexpr bool InterproceduralRead = true;

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;

 private:
  // blocks the loop plans claimed, straight line plans leave them alone
  using Claimed = TVec<fir::BasicBlock>;

  bool vectorize_loops(fir::Context &ctx, fir::Function &func, AliasAnalyis &aa,
                       Claimed &claimed);
  // what the straight line plans of one block would save
  i64 slp_benefit(fir::Context &ctx, AliasAnalyis &aa, fir::BasicBlock bb);
};

}  // namespace foptim::optim
