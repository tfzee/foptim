#include "optim/func_passes/vectorizer/region.hpp"

#include <algorithm>

#include "optim/analysis/cfg.hpp"

namespace foptim::optim::vec {

RegionTree::RegionTree(CFG &cfg, TVec<LoopInfo> &loops) {
  regions.resize(loops.size());
  for (u32 i = 0; i < loops.size(); i++) {
    regions[i].loop = &loops[i];
  }
  // the innermost loop of a block is the last one that contains it when the
  // loops are walked from the biggest to the smallest
  TVec<u32> order;
  for (u32 i = 0; i < loops.size(); i++) {
    order.push_back(i);
  }
  std::ranges::stable_sort(order, [&](u32 a, u32 b) {
    return loops[a].body_nodes.size() > loops[b].body_nodes.size();
  });
  TVec<i32> owner;
  owner.resize(cfg.bbrs.size(), -1);
  for (auto r : order) {
    regions[r].parent = owner[loops[r].head];
    for (auto b : loops[r].body_nodes) {
      owner[b] = static_cast<i32>(r);
    }
  }
  for (u32 i = 0; i < loops.size(); i++) {
    if (regions[i].parent < 0) {
      roots.push_back(i);
    } else {
      regions[static_cast<u32>(regions[i].parent)].children.push_back(i);
    }
  }
  for (u32 b = 0; b < owner.size(); b++) {
    if (owner[b] >= 0) {
      regions[static_cast<u32>(owner[b])].own_blocks.push_back(cfg.bbrs[b].bb);
    }
  }
}

namespace {
struct Selector {
  RegionTree &tree;
  u32 assumed_trip;
  const std::function<i64(fir::BasicBlock)> &block_benefit;
  TVec<std::optional<i64>> best_memo;
  TVec<bool> use_plan;

  i64 trip(const LoopRegion &r) const {
    if (r.plan != nullptr && r.plan->loop.const_trip) {
      return static_cast<i64>(*r.plan->loop.const_trip);
    }
    return assumed_trip;
  }

  i64 best(u32 id) {
    if (best_memo[id]) {
      return *best_memo[id];
    }
    const auto &r = tree.regions[id];
    i64 per_iter = 0;
    for (auto b : r.own_blocks) {
      per_iter += block_benefit(b);
    }
    for (auto c : r.children) {
      per_iter += best(c);
    }
    i64 value = trip(r) * per_iter;
    if (r.plan != nullptr && r.plan->benefit >= value) {
      value = r.plan->benefit;
      use_plan[id] = true;
    }
    best_memo[id] = value;
    return value;
  }

  void decide(u32 id, TVec<u32> &chosen) {
    const auto &r = tree.regions[id];
    if (r.plan != nullptr) {
      best(id);
      if (use_plan[id]) {
        chosen.push_back(id);
        return;
      }
    }
    for (auto c : r.children) {
      decide(c, chosen);
    }
  }
};
}  // namespace

TVec<u32> select_plans(
    RegionTree &tree, u32 assumed_trip,
    const std::function<i64(fir::BasicBlock)> &block_benefit) {
  Selector sel{tree, assumed_trip, block_benefit, {}, {}};
  sel.best_memo.resize(tree.regions.size());
  sel.use_plan.resize(tree.regions.size(), false);
  TVec<u32> chosen;
  for (auto r : tree.roots) {
    sel.decide(r, chosen);
  }
  std::ranges::sort(chosen);
  return chosen;
}

}  // namespace foptim::optim::vec
