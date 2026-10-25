#pragma once
#include <functional>

#include "optim/func_passes/vectorizer/plan.hpp"

namespace foptim::optim::vec {

// The loop nest of a function. LoopRegion i is loop i of the
// LoopInfoAnalysis, the blocks outside of every loop belong to no region. A
// region may have a loop plan that vectorizes the whole loop (it then claims
// its whole body).
struct LoopRegion {
  LoopInfo *loop = nullptr;
  i32 parent = -1;
  TVec<u32> children;
  // blocks of this loop that are not in a nested loop
  TVec<fir::BasicBlock> own_blocks;
  Plan *plan = nullptr;
};

class RegionTree {
 public:
  TVec<LoopRegion> regions;
  TVec<u32> roots;

  RegionTree() = default;
  RegionTree(CFG &cfg, TVec<LoopInfo> &loops);
};

// The regions that run their loop plan. Per entry of a region
//   best(loop) = max(loop plan, trip * (sum best(children) + straight line
//   plans of the own blocks))
// where block_benefit estimates what the straight line plans save in one
// execution of a block. Loop plans of regions that are not picked (the
// nested ones) are dropped, the picked ones are returned in region order.
TVec<u32> select_plans(
    RegionTree &tree, u32 assumed_trip,
    const std::function<i64(fir::BasicBlock)> &block_benefit);

}  // namespace foptim::optim::vec
