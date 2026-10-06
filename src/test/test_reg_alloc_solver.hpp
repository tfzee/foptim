#include <gtest/gtest.h>

#include <set>

#include "mir/optim/calling_conv.hpp"
#include "mir/optim/reg_alloc_solver.hpp"

namespace {
using namespace foptim;
using namespace foptim::fmir;

// Builds small PBQP problems by hand and runs the register allocation solver
// (minimize_graph + actually_allocate) on them.
struct SolverFixture {
  CostGraph g;
  TVec<StackRecord> stack;
  TVec<CReg> assignments;
  TVec<u64> spilled;
  u64 next_vreg = 0;

  u32 vreg(u8 k = kind::Gpr) {
    u32 n = g.node(reg_to_uid(VReg{next_vreg++}));
    g.set_kind(n, k);
    return n;
  }
  u32 concrete(CReg r) {
    u32 n = g.node(reg_to_uid(VReg{r}));
    g.set_kind(n, kind_of(VReg{r}));
    return n;
  }
  // like the graph setup: interference first, hints on top of it
  void interfere(u32 a, u32 b) {
    ASSERT(!g.has_conn(a, b));
    g.add_edge(a, b, CostMatrix{});
  }
  // make `r` cheaper than every other register for node `n`
  void prefer(u32 n, CReg r, f32 strength = 10.F) {
    const auto &dom = kKinds[g.node_kind[n]];
    for (u32 i = 0; i < dom.size; i++) {
      if (dom.regs[i] != static_cast<u8>(r) && dom.regs[i] != 0) {
        g.node_costs[n].cost[i] += strength;
      }
    }
  }
  void solve() {
    minimize_graph(g, stack);
    actually_allocate(g, stack, assignments, spilled);
  }
  [[nodiscard]] CReg reg(u32 n) const { return assignments[n]; }
  [[nodiscard]] bool is_gpr(u32 n) const {
    return reg(n) >= CReg::A && reg(n) <= CReg::R15;
  }
  [[nodiscard]] bool is_vec(u32 n) const { return reg(n) >= CReg::mm0; }
};
} // namespace

TEST(RegAllocDomains, GprDomain) {
  const auto &d = kKinds[kind::Gpr];
  EXPECT_EQ(d.size, 15);
  EXPECT_EQ(d.regs[0], static_cast<u8>(CReg::Virtual));
  for (u32 i = 0; i < d.size; i++) {
    EXPECT_NE(d.regs[i], static_cast<u8>(CReg::SP));
    EXPECT_NE(d.regs[i], static_cast<u8>(CReg::BP));
    EXPECT_LE(d.regs[i], static_cast<u8>(CReg::R15));
    if (i > 0) {
      EXPECT_LT(d.regs[i - 1], d.regs[i]);
    }
  }
}

TEST(RegAllocDomains, VecAndMixedDomain) {
  EXPECT_EQ(kKinds[kind::Vec].size, 17);
  EXPECT_EQ(kKinds[kind::Vec].regs[0], 0);
  EXPECT_EQ(kKinds[kind::Vec].regs[1], static_cast<u8>(CReg::mm0));
  EXPECT_EQ(kKinds[kind::GprOrVec].size, kMaxDomain);
}

TEST(RegAllocDomains, ConcreteDomainIsItsRegister) {
  for (auto r : {CReg::A, CReg::SP, CReg::R15, CReg::mm7}) {
    const auto &d = kKinds[kind_of(VReg{r})];
    EXPECT_EQ(d.size, 1);
    EXPECT_EQ(d.regs[0], static_cast<u8>(r));
    EXPECT_EQ(d.local[static_cast<u8>(r)], 0);
    EXPECT_EQ(d.local[0], kNoLocal);
  }
}

TEST(RegAllocDomains, LocalIsInverseOfRegs) {
  for (u32 k = 0; k < kind::N; k++) {
    const auto &d = kKinds[k];
    u32 present = 0;
    for (u32 r = 0; r < numRegs; r++) {
      if (d.local[r] != kNoLocal) {
        present++;
        EXPECT_EQ(d.regs[d.local[r]], r);
      }
    }
    EXPECT_EQ(present, d.size);
  }
}

TEST(RegAllocMatrix, ImplicitMatrixIsInterference) {
  CostMatrix m;
  const auto a = static_cast<u32>(CReg::A);
  const auto b = static_cast<u32>(CReg::B);
  EXPECT_EQ(m.get_cost(true, a, a), infCost);
  EXPECT_EQ(m.get_cost(true, a, b), 0.F);
  // both spilled is fine
  EXPECT_EQ(m.get_cost(true, 0, 0), 0.F);
}

// the rows of a matrix belong to the lower node, looking at it from the other
// node has to give the same costs
TEST(RegAllocMatrix, OrientationOfAsymmetricMatrix) {
  const u8 k_lo = kind::Gpr;
  const u8 k_hi = kind::Vec;
  const u32 n_lo = dom_size(k_lo);
  const u32 n_hi = dom_size(k_hi);
  DenseCosts dense = {};
  for (u32 l = 0; l < n_lo; l++) {
    for (u32 h = 0; h < n_hi; h++) {
      dense[l][h] = static_cast<f32>((l * 100) + h);
    }
  }
  CostMatrix m{.cost = CostMatrix::store(dense, n_lo, n_hi),
               .hard_constraint = true,
               .kind_lo = k_lo,
               .kind_hi = k_hi};
  for (u32 l = 0; l < n_lo; l++) {
    for (u32 h = 0; h < n_hi; h++) {
      const u32 lo_reg = kKinds[k_lo].regs[l];
      const u32 hi_reg = kKinds[k_hi].regs[h];
      const auto expected = static_cast<f32>((l * 100) + h);
      EXPECT_EQ(m.get_cost(true, lo_reg, hi_reg), expected);
      EXPECT_EQ(m.get_cost(false, hi_reg, lo_reg), expected);
    }
  }

  DenseCosts as_lo;
  DenseCosts as_hi;
  m.to_dense(true, k_lo, k_hi, as_lo);
  m.to_dense(false, k_hi, k_lo, as_hi);
  for (u32 l = 0; l < n_lo; l++) {
    for (u32 h = 0; h < n_hi; h++) {
      EXPECT_EQ(as_lo[l][h], dense[l][h]);
      EXPECT_EQ(as_hi[h][l], dense[l][h]);
    }
  }
}

TEST(RegAllocMatrix, CopyDiscountOnlyWhereRegistersMatch) {
  SolverFixture f;
  u32 a = f.vreg();
  u32 b = f.vreg();
  f.g.add_copy_discount(a, b);
  f.g.add_copy_discount(a, b);
  const auto &m = f.g.edges[f.g.find_edge(a, b)];
  EXPECT_FALSE(m.hard_constraint);
  const auto rbx = static_cast<u32>(CReg::B);
  const auto rax = static_cast<u32>(CReg::A);
  EXPECT_EQ(m.get_cost(true, rbx, rbx), 2 * copyDiscount);
  EXPECT_EQ(m.get_cost(true, rax, rax), 2 * copyDiscount);
  EXPECT_EQ(m.get_cost(true, rax, rbx), 0.F);
  EXPECT_EQ(m.get_cost(true, 0, 0), 0.F);
}

TEST(RegAllocMatrix, CopyDiscountDoesNotRelaxInterference) {
  SolverFixture f;
  u32 a = f.vreg();
  u32 b = f.vreg();
  f.interfere(a, b);
  f.g.add_copy_discount(a, b);
  const auto &m = f.g.edges[f.g.find_edge(a, b)];
  EXPECT_EQ(m.get_cost(true, static_cast<u32>(CReg::A),
                       static_cast<u32>(CReg::A)),
            infCost);
}

TEST(RegAllocSolver, IndependentNodesAreAssigned) {
  SolverFixture f;
  u32 a = f.vreg();
  u32 v = f.vreg(kind::Vec);
  f.solve();
  EXPECT_TRUE(f.is_gpr(a));
  EXPECT_TRUE(f.is_vec(v));
  EXPECT_TRUE(f.spilled.empty());
}

TEST(RegAllocSolver, TriangleGetsDistinctRegisters) {
  SolverFixture f;
  u32 a = f.vreg();
  u32 b = f.vreg();
  u32 c = f.vreg();
  f.interfere(a, b);
  f.interfere(b, c);
  f.interfere(a, c);
  f.solve();
  std::set<CReg> regs{f.reg(a), f.reg(b), f.reg(c)};
  EXPECT_EQ(regs.size(), 3);
  EXPECT_FALSE(regs.contains(CReg::Virtual));
  EXPECT_TRUE(f.spilled.empty());
}

TEST(RegAllocSolver, FourteenLiveValuesFit) {
  SolverFixture f;
  TVec<u32> nodes;
  for (int i = 0; i < 14; i++) {
    nodes.push_back(f.vreg());
  }
  for (size_t i = 0; i < nodes.size(); i++) {
    for (size_t j = i + 1; j < nodes.size(); j++) {
      f.interfere(nodes[i], nodes[j]);
    }
  }
  f.solve();
  std::set<CReg> regs;
  for (auto n : nodes) {
    regs.insert(f.reg(n));
  }
  EXPECT_EQ(regs.size(), 14);
  EXPECT_FALSE(regs.contains(CReg::Virtual));
  EXPECT_FALSE(regs.contains(CReg::SP));
  EXPECT_FALSE(regs.contains(CReg::BP));
  EXPECT_TRUE(f.spilled.empty());
}

TEST(RegAllocSolver, FifteenLiveValuesSpillOne) {
  SolverFixture f;
  TVec<u32> nodes;
  for (int i = 0; i < 15; i++) {
    nodes.push_back(f.vreg());
  }
  for (size_t i = 0; i < nodes.size(); i++) {
    for (size_t j = i + 1; j < nodes.size(); j++) {
      f.interfere(nodes[i], nodes[j]);
    }
  }
  f.solve();
  EXPECT_EQ(f.spilled.size(), 1);
  std::set<CReg> regs;
  for (auto n : nodes) {
    if (f.reg(n) != CReg::Virtual) {
      EXPECT_TRUE(regs.insert(f.reg(n)).second);
    }
  }
  EXPECT_EQ(regs.size(), 14);
}

TEST(RegAllocSolver, VecAndGprPressureAreIndependent) {
  SolverFixture f;
  TVec<u32> gprs;
  TVec<u32> vecs;
  for (int i = 0; i < 14; i++) {
    gprs.push_back(f.vreg());
  }
  for (int i = 0; i < 16; i++) {
    vecs.push_back(f.vreg(kind::Vec));
  }
  TVec<u32> all = gprs;
  all.insert(all.end(), vecs.begin(), vecs.end());
  for (size_t i = 0; i < all.size(); i++) {
    for (size_t j = i + 1; j < all.size(); j++) {
      f.interfere(all[i], all[j]);
    }
  }
  f.solve();
  EXPECT_TRUE(f.spilled.empty());
  for (auto n : gprs) {
    EXPECT_TRUE(f.is_gpr(n));
  }
  for (auto n : vecs) {
    EXPECT_TRUE(f.is_vec(n));
  }
}

TEST(RegAllocSolver, PrecoloredInterferenceIsRespected) {
  SolverFixture f;
  u32 rax = f.concrete(CReg::A);
  u32 rbx = f.concrete(CReg::B);
  u32 v = f.vreg();
  f.interfere(v, rax);
  f.interfere(v, rbx);
  f.solve();
  EXPECT_NE(f.reg(v), CReg::A);
  EXPECT_NE(f.reg(v), CReg::B);
  EXPECT_TRUE(f.is_gpr(v));
}

TEST(RegAllocSolver, SpillsWhenAllRegistersAreTaken) {
  SolverFixture f;
  u32 v = f.vreg();
  for (auto r : kAllocatableGPRegs) {
    f.interfere(v, f.concrete(r));
  }
  f.solve();
  EXPECT_EQ(f.reg(v), CReg::Virtual);
  ASSERT_EQ(f.spilled.size(), 1);
  EXPECT_EQ(f.spilled[0], f.g.node_uid[v]);
}

TEST(RegAllocSolver, CopyHintPicksTheSameRegister) {
  // without the hint b takes rax (lowest register), with it b follows a
  {
    SolverFixture f;
    u32 a = f.vreg();
    u32 b = f.vreg();
    f.prefer(a, CReg::B);
    f.solve();
    EXPECT_EQ(f.reg(a), CReg::B);
    EXPECT_EQ(f.reg(b), CReg::A);
  }
  {
    SolverFixture f;
    u32 a = f.vreg();
    u32 b = f.vreg();
    f.prefer(a, CReg::B);
    f.g.add_copy_discount(a, b);
    f.solve();
    EXPECT_EQ(f.reg(a), CReg::B);
    EXPECT_EQ(f.reg(b), CReg::B);
  }
}

TEST(RegAllocSolver, CopyHintToPrecoloredRegister) {
  // like `mov rdi, v`: v should be allocated to rdi if nothing is in the way
  SolverFixture f;
  u32 rdi = f.concrete(CReg::DI);
  u32 v = f.vreg();
  f.g.add_copy_discount(v, rdi);
  f.solve();
  EXPECT_EQ(f.reg(v), CReg::DI);
}

TEST(RegAllocSolver, CopyHintLosesAgainstInterference) {
  SolverFixture f;
  u32 rdi = f.concrete(CReg::DI);
  u32 v = f.vreg();
  f.interfere(v, rdi);
  f.g.add_copy_discount(v, rdi);
  f.solve();
  EXPECT_NE(f.reg(v), CReg::DI);
  EXPECT_TRUE(f.is_gpr(v));
}

// chain gpr - vec - vec - gpr: reducing the middle nodes builds matrices
// between nodes with different domains, which are not square and not
// symmetric, and these are used again for later reductions
TEST(RegAllocSolver, MixedDomainChainFollowsPreferences) {
  SolverFixture f;
  u32 g0 = f.vreg(kind::Gpr);
  u32 v1 = f.vreg(kind::Vec);
  u32 v2 = f.vreg(kind::Vec);
  u32 g3 = f.vreg(kind::Gpr);
  f.interfere(g0, v1);
  f.interfere(v1, v2);
  f.interfere(v2, g3);
  f.prefer(g0, CReg::R9);
  f.prefer(v1, CReg::mm3);
  f.prefer(v2, CReg::mm5);
  f.prefer(g3, CReg::C);
  f.solve();
  EXPECT_EQ(f.reg(g0), CReg::R9);
  EXPECT_EQ(f.reg(v1), CReg::mm3);
  EXPECT_EQ(f.reg(v2), CReg::mm5);
  EXPECT_EQ(f.reg(g3), CReg::C);
  EXPECT_TRUE(f.spilled.empty());
}

TEST(RegAllocSolver, MixedDomainChainResolvesConflict) {
  SolverFixture f;
  u32 g0 = f.vreg(kind::Gpr);
  u32 v1 = f.vreg(kind::Vec);
  u32 v2 = f.vreg(kind::Vec);
  u32 g3 = f.vreg(kind::Gpr);
  f.interfere(g0, v1);
  f.interfere(v1, v2);
  f.interfere(v2, g3);
  f.prefer(v1, CReg::mm3);
  f.prefer(v2, CReg::mm3);
  f.solve();
  EXPECT_TRUE(f.is_vec(v1));
  EXPECT_TRUE(f.is_vec(v2));
  EXPECT_NE(f.reg(v1), f.reg(v2));
  // exactly one of them gets the preferred register
  EXPECT_EQ((f.reg(v1) == CReg::mm3) + (f.reg(v2) == CReg::mm3), 1);
  EXPECT_TRUE(f.spilled.empty());
}

TEST(RegAllocSolver, CycleOfInterferenceIsColored) {
  // a cycle can not be reduced by degree 1 nodes alone, R2 has to merge edges
  SolverFixture f;
  constexpr int N = 9;
  TVec<u32> nodes;
  for (int i = 0; i < N; i++) {
    nodes.push_back(f.vreg(i % 2 == 0 ? kind::Gpr : kind::Vec));
  }
  for (int i = 0; i < N; i++) {
    f.interfere(nodes[i], nodes[(i + 1) % N]);
  }
  f.solve();
  EXPECT_TRUE(f.spilled.empty());
  for (int i = 0; i < N; i++) {
    EXPECT_NE(f.reg(nodes[i]), f.reg(nodes[(i + 1) % N]));
    EXPECT_NE(f.reg(nodes[i]), CReg::Virtual);
  }
}

TEST(RegAllocSolver, DeterministicAssignment) {
  auto run = []() {
    SolverFixture f;
    TVec<u32> nodes;
    for (int i = 0; i < 20; i++) {
      nodes.push_back(f.vreg(i % 3 == 0 ? kind::Vec : kind::Gpr));
    }
    for (int i = 0; i < 20; i++) {
      for (int j = i + 1; j < 20; j++) {
        if ((i * 7 + j * 3) % 4 != 0) {
          f.interfere(nodes[i], nodes[j]);
        }
      }
    }
    f.solve();
    std::vector<int> out;
    for (auto n : nodes) {
      out.push_back(static_cast<int>(f.reg(n)));
    }
    return out;
  };
  EXPECT_EQ(run(), run());
}

namespace {
const std::array<u8, numRegs> &test_save_classes() {
  static const auto table = CCallHelper.save_classes();
  return table;
}

f32 cost_of(const CostVector &v, u8 k, CReg r) {
  return v.cost[kKinds[k].local[static_cast<u8>(r)]];
}
} // namespace

TEST(RegAllocCostModel, SpillCostIsWeightedUseCount) {
  CostVector v = initial_costs(kind::Gpr);
  apply_cost_model(v, kind::Gpr, {.use_weight = 24.F}, test_save_classes());
  EXPECT_EQ(v.cost[0], 24.F * kMemOpCost);
  // never free, there is always a load or store
  apply_cost_model(v, kind::Gpr, {.use_weight = 0.F}, test_save_classes());
  EXPECT_EQ(v.cost[0], kMinSpillCost);
}

TEST(RegAllocCostModel, UnspillableIsExpensive) {
  CostVector v = initial_costs(kind::Gpr);
  apply_cost_model(v, kind::Gpr, {.use_weight = 2.F, .unspillable = true},
                   test_save_classes());
  EXPECT_EQ(v.cost[0], kUnspillableCost);
}

TEST(RegAllocCostModel, CallerSavedCostScalesWithCallsCrossed) {
  CostVector none = initial_costs(kind::Gpr);
  CostVector some = initial_costs(kind::Gpr);
  apply_cost_model(none, kind::Gpr, {.use_weight = 2.F}, test_save_classes());
  apply_cost_model(some, kind::Gpr,
                   {.use_weight = 2.F, .call_crossings = 3.F},
                   test_save_classes());
  for (auto r : {CReg::A, CReg::C, CReg::D, CReg::SI, CReg::DI, CReg::R8,
                 CReg::R11}) {
    EXPECT_EQ(cost_of(none, kind::Gpr, r), 0.F);
    EXPECT_EQ(cost_of(some, kind::Gpr, r), 3.F * kPushPopCost);
  }
}

TEST(RegAllocCostModel, CalleeSavedCostIsFlat) {
  CostVector none = initial_costs(kind::Gpr);
  CostVector some = initial_costs(kind::Gpr);
  apply_cost_model(none, kind::Gpr, {.use_weight = 2.F}, test_save_classes());
  apply_cost_model(some, kind::Gpr,
                   {.use_weight = 2.F, .call_crossings = 100.F},
                   test_save_classes());
  for (auto r : {CReg::B, CReg::R12, CReg::R13, CReg::R14, CReg::R15}) {
    EXPECT_EQ(cost_of(none, kind::Gpr, r), kCalleeSavedCost);
    EXPECT_EQ(cost_of(some, kind::Gpr, r), kCalleeSavedCost);
  }
}

TEST(RegAllocCostModel, VecRegistersAreAllCallerSaved) {
  CostVector v = initial_costs(kind::Vec);
  apply_cost_model(v, kind::Vec, {.use_weight = 2.F, .call_crossings = 2.F},
                   test_save_classes());
  for (u32 l = 1; l < dom_size(kind::Vec); l++) {
    EXPECT_EQ(v.cost[l], 2.F * kPushPopCost);
  }
}

TEST(RegAllocCostModel, ValueAcrossCallsPrefersCalleeSaved) {
  SolverFixture f;
  u32 v = f.vreg();
  apply_cost_model(f.g.node_costs[v], kind::Gpr,
                   {.use_weight = 4.F, .call_crossings = 8.F},
                   test_save_classes());
  f.solve();
  EXPECT_TRUE(f.reg(v) == CReg::B || f.reg(v) >= CReg::R12);
  EXPECT_TRUE(f.spilled.empty());
}

TEST(RegAllocCostModel, ValueWithoutCallsAvoidsCalleeSaved) {
  SolverFixture f;
  u32 v = f.vreg();
  apply_cost_model(f.g.node_costs[v], kind::Gpr, {.use_weight = 4.F},
                   test_save_classes());
  f.solve();
  EXPECT_NE(f.reg(v), CReg::B);
  EXPECT_LT(f.reg(v), CReg::R12);
}

TEST(RegAllocCostModel, CheapestValueIsSpilled) {
  // 15 values live at the same time, the one used least often is the one that
  // ends up on the stack
  for (int cheapest = 0; cheapest < 15; cheapest += 7) {
    SolverFixture f;
    TVec<u32> nodes;
    for (int i = 0; i < 15; i++) {
      u32 n = f.vreg();
      apply_cost_model(f.g.node_costs[n], kind::Gpr,
                       {.use_weight = i == cheapest ? 2.F : 16.F},
                       test_save_classes());
      nodes.push_back(n);
    }
    for (size_t i = 0; i < nodes.size(); i++) {
      for (size_t j = i + 1; j < nodes.size(); j++) {
        f.interfere(nodes[i], nodes[j]);
      }
    }
    f.solve();
    ASSERT_EQ(f.spilled.size(), 1);
    EXPECT_EQ(f.spilled[0], f.g.node_uid[nodes[cheapest]]);
  }
}

TEST(RegAllocCostModel, SpillTemporariesAreNotSpilled) {
  // 15 live values, all weights equal but one of them is a spill temporary
  SolverFixture f;
  TVec<u32> nodes;
  for (int i = 0; i < 15; i++) {
    u32 n = f.vreg();
    apply_cost_model(f.g.node_costs[n], kind::Gpr,
                     {.use_weight = 2.F, .unspillable = i == 0},
                     test_save_classes());
    nodes.push_back(n);
  }
  for (size_t i = 0; i < nodes.size(); i++) {
    for (size_t j = i + 1; j < nodes.size(); j++) {
      f.interfere(nodes[i], nodes[j]);
    }
  }
  f.solve();
  ASSERT_EQ(f.spilled.size(), 1);
  EXPECT_NE(f.spilled[0], f.g.node_uid[nodes[0]]);
}
