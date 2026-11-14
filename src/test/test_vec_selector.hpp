#include <gtest/gtest.h>

#include "optim/func_passes/vectorizer/region.hpp"

namespace {
using namespace foptim;
using namespace foptim::optim::vec;

constexpr u32 AssumedTrip = 64;

// a nesting without a CFG: parent links, plans and the benefit of a block in
// one execution are all the selector looks at
struct SelectorCase {
  foptim::optim::LoopNesting nest;
  // the plans the selector sees, one entry per loop
  TVec<Plan *> loop_plans;
  // owns what loop_plans points at
  std::deque<Plan> plans;

  u32 add(i32 parent, std::optional<i64> plan_benefit,
          std::optional<i128> const_trip = std::nullopt, u32 n_blocks = 0) {
    const auto id = static_cast<u32>(nest.size());
    nest.parent.push_back(parent);
    nest.children.emplace_back();
    nest.own_blocks.emplace_back();
    loop_plans.push_back(nullptr);
    if (parent < 0) {
      nest.roots.push_back(id);
    } else {
      nest.children[static_cast<u32>(parent)].push_back(id);
    }
    if (plan_benefit) {
      Plan p;
      p.space = LaneSpace::LoopIter;
      p.benefit = *plan_benefit;
      p.loop.const_trip = const_trip;
      plans.push_back(std::move(p));
      loop_plans[id] = &plans.back();
    }
    // the blocks are only counted, the benefit callback ignores which one
    for (u32 i = 0; i < n_blocks; i++) {
      nest.own_blocks[id].push_back(0);
    }
    return id;
  }
};

// every own block saves `per_block` in one execution
TVec<u32> select(SelectorCase &c, i64 per_block = 0) {
  return select_plans(c.nest, c.loop_plans, AssumedTrip,
                      [&](u32) -> i64 { return per_block; });
}
}  // namespace

TEST(VecSelector, LoopPlanWinsOverCheapBodyPlans) {
  SelectorCase c;
  c.add(-1, 100);
  // no own blocks, so the straight line plans of the body save nothing
  EXPECT_EQ(select(c), (TVec<u32>{0}));
}

TEST(VecSelector, BodyPlansBeatTheLoopPlan) {
  {
    SelectorCase c;
    c.add(-1, 100, std::nullopt, 1);
    // 64 assumed iterations of 1 is less than 100
    EXPECT_EQ(select(c, 1), (TVec<u32>{0}));
  }
  {
    SelectorCase c;
    c.add(-1, 100, std::nullopt, 1);
    EXPECT_TRUE(select(c, 5).empty());
  }
  {
    // a known trip count is used instead of the assumed one
    SelectorCase c;
    c.add(-1, 100, 4, 1);
    EXPECT_EQ(select(c, 5), (TVec<u32>{0}));
  }
}

TEST(VecSelector, NoPlanNothingChosen) {
  SelectorCase c;
  c.add(-1, std::nullopt);
  EXPECT_TRUE(select(c).empty());
}

TEST(VecSelector, InnerLoopsAreIndependent) {
  SelectorCase c;
  const auto outer = c.add(-1, std::nullopt);
  c.add(static_cast<i32>(outer), 10);
  c.add(static_cast<i32>(outer), 20);
  EXPECT_EQ(select(c), (TVec<u32>{1, 2}));
}

TEST(VecSelector, OuterPlanAgainstSumOfInnerPlans) {
  {
    SelectorCase c;
    const auto outer = c.add(-1, 1000, 10);
    c.add(static_cast<i32>(outer), 30);
    c.add(static_cast<i32>(outer), 20);
    // 10 iterations of 30 + 20 = 500 < 1000
    EXPECT_EQ(select(c), (TVec<u32>{0}));
  }
  {
    SelectorCase c;
    const auto outer = c.add(-1, 400, 10);
    c.add(static_cast<i32>(outer), 30);
    c.add(static_cast<i32>(outer), 20);
    EXPECT_EQ(select(c), (TVec<u32>{1, 2}));
  }
}

TEST(VecSelector, TieKeepsTheLoopPlan) {
  SelectorCase c;
  const auto outer = c.add(-1, 500, 10);
  c.add(static_cast<i32>(outer), 50);
  EXPECT_EQ(select(c), (TVec<u32>{0}));
}
