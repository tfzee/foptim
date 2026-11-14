#pragma once
#include <functional>

#include "optim/analysis/loop_analysis.hpp"
#include "optim/func_passes/vectorizer/plan.hpp"

namespace foptim::optim::vec {

// The regions that run their loop plan. `plans` holds one plan per loop of the
// nesting (null where the loop has none), a plan vectorizes the whole loop and
// then claims its whole body. Per entry of a region
//   best(loop) = max(loop plan, trip * (sum best(children) + straight line
//   plans of the own blocks))
// where block_benefit estimates what the straight line plans save in one
// execution of a block. Loop plans of regions that are not picked (the
// nested ones) are dropped, the picked ones are returned in loop order.
TVec<u32> select_plans(const LoopNesting &nest, const TVec<Plan *> &plans,
                       u32 assumed_trip,
                       const std::function<i64(u32)> &block_benefit);

}  // namespace foptim::optim::vec
