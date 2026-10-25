#include <gtest/gtest.h>

#include "optim/func_passes/vectorizer/region.hpp"

namespace {
using namespace foptim;
using namespace foptim::optim::vec;

constexpr u32 AssumedTrip = 64;

// regions without a CFG: parent links, plans and the benefit of a block in
// one execution are all the selector looks at
struct SelectorCase {
  RegionTree tree;
  // plans are referenced by the regions
  std::deque<Plan> plans;

  u32 add(i32 parent, std::optional<i64> plan_benefit,
          std::optional<i128> const_trip = std::nullopt, u32 n_blocks = 0) {
    const auto id = static_cast<u32>(tree.regions.size());
    LoopRegion r;
    r.parent = parent;
    if (parent < 0) {
      tree.roots.push_back(id);
    } else {
      tree.regions[static_cast<u32>(parent)].children.push_back(id);
    }
    if (plan_benefit) {
      Plan p;
      p.space = LaneSpace::LoopIter;
      p.benefit = *plan_benefit;
      p.loop.const_trip = const_trip;
      plans.push_back(std::move(p));
      r.plan = &plans.back();
    }
    // the blocks are only counted, the benefit callback ignores which one
    for (u32 i = 0; i < n_blocks; i++) {
      r.own_blocks.push_back(fir::BasicBlock{fir::BasicBlock::invalid()});
    }
    tree.regions.push_back(std::move(r));
    return id;
  }
};

// every own block saves `per_block` in one execution
TVec<u32> select(SelectorCase &c, i64 per_block = 0) {
  return select_plans(c.tree, AssumedTrip,
                      [&](fir::BasicBlock) -> i64 { return per_block; });
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
