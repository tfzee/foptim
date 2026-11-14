#include "optim/func_passes/vectorizer/region.hpp"

#include <algorithm>

namespace foptim::optim::vec {

namespace {
struct Selector {
  const LoopNesting &nest;
  const TVec<Plan *> &plans;
  u32 assumed_trip;
  const std::function<i64(u32)> &block_benefit;
  TVec<std::optional<i64>> best_memo;
  TVec<bool> use_plan;

  i64 trip(u32 id) const {
    const Plan *p = plans[id];
    if (p != nullptr && p->loop.const_trip) {
      return static_cast<i64>(*p->loop.const_trip);
    }
    return assumed_trip;
  }

  i64 best(u32 id) {
    if (best_memo[id]) {
      return *best_memo[id];
    }
    i64 per_iter = 0;
    for (auto b : nest.own_blocks[id]) {
      per_iter += block_benefit(b);
    }
    for (auto c : nest.children[id]) {
      per_iter += best(c);
    }
    i64 value = trip(id) * per_iter;
    if (plans[id] != nullptr && plans[id]->benefit >= value) {
      value = plans[id]->benefit;
      use_plan[id] = true;
    }
    best_memo[id] = value;
    return value;
  }

  void decide(u32 id, TVec<u32> &chosen) {
    if (plans[id] != nullptr) {
      best(id);
      if (use_plan[id]) {
        chosen.push_back(id);
        return;
      }
    }
    for (auto c : nest.children[id]) {
      decide(c, chosen);
    }
  }
};
}  // namespace

TVec<u32> select_plans(const LoopNesting &nest, const TVec<Plan *> &plans,
                       u32 assumed_trip,
                       const std::function<i64(u32)> &block_benefit) {
  Selector sel{nest, plans, assumed_trip, block_benefit, {}, {}};
  sel.best_memo.resize(nest.size());
  sel.use_plan.resize(nest.size(), false);
  TVec<u32> chosen;
  for (auto r : nest.roots) {
    sel.decide(r, chosen);
  }
  std::ranges::sort(chosen);
  return chosen;
}

}  // namespace foptim::optim::vec
