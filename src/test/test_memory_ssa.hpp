#pragma once
#include <gtest/gtest.h>

#include <functional>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/memory_ssa.hpp"

namespace {
using foptim::fir::ValueR;
using foptim::optim::AliasQuery;
using foptim::optim::AnalysisManager;
using foptim::optim::MemLoc;
using foptim::optim::MemorySSA;
using foptim::optim::MemorySSAWalker;
using foptim::optim::PreservedAnalysis;

// f(p noalias, q noalias, r, cond), blocks are made by the tests. r is a plain
// pointer argument, which cannot point into the objects of the noalias ones.
struct MemSSAModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR i32{ctx.data->get_int_type(32)};
  foptim::fir::TypeR i1{ctx.data->get_int_type(1)};
  foptim::fir::TypeR i64{ctx.data->get_int_type(64)};
  foptim::fir::TypeR ptr{ctx.data->get_ptr_type()};
  foptim::fir::FunctionR f;
  foptim::fir::Builder b;
  ValueR p, q, r, cond;

  ValueR c32(foptim::i64 v) {
    return ValueR{ctx.data->get_constant_value(v, i32)};
  }
  ValueR load(ValueR addr) { return b.build_load(i32, addr, false, false); }
  foptim::fir::Instr load_i(ValueR addr) { return load(addr).as_instr(); }
  foptim::fir::Instr store(ValueR addr, foptim::i64 v = 1) {
    return b.build_store(addr, c32(v), false, false).as_instr();
  }

  MemSSAModule()
      : f(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_void_type(),
                                       {ptr, ptr, ptr, i1}))),
        b(f.builder()) {
    auto entry = f.func->get_entry();
    b.at_end(entry);
    p = ValueR{entry->args[0]};
    q = ValueR{entry->args[1]};
    r = ValueR{entry->args[2]};
    cond = ValueR{entry->args[3]};
    entry->args[0]->noalias = 1;
    entry->args[1]->noalias = 1;
  }

  MemorySSA &ssa() { return AnalysisManager::memory_ssa(*f.func); }
  bool valid() {
    return ssa().verify(AnalysisManager::cfg(*f.func),
                        AnalysisManager::dom(*f.func));
  }
  // clobber of a load, queried with a fresh walker
  foptim::u32 clobber(foptim::fir::Instr load,
                      foptim::u32 max_steps = MemorySSAWalker::MaxSteps) {
    AliasQuery aq{AnalysisManager::provenance(*f.func)};
    MemorySSAWalker w{ssa(), AnalysisManager::mem_accesses(*f.func), aq,
                      max_steps};
    return w.clobber(load);
  }
  // ends in the join block, the bodies run in the arms
  void diamond(const std::function<void()> &then_body = {},
               const std::function<void()> &else_body = {}) {
    auto then_bb = b.append_bb();
    auto else_bb = b.append_bb();
    auto join = b.append_bb();
    b.build_cond_branch(cond, then_bb, else_bb);
    b.at_end(then_bb);
    if (then_body) {
      then_body();
    }
    b.build_branch(join);
    b.at_end(else_bb);
    if (else_body) {
      else_body();
    }
    b.build_branch(join);
    b.at_end(join);
  }
};

TEST(MemorySSA, StraightLine) {
  MemSSAModule m;
  auto st_p = m.store(m.p);
  auto ld_q = m.load_i(m.q);
  auto ld_p = m.load_i(m.p);
  auto st_r = m.store(m.p, 2);
  auto ld_p2 = m.load_i(m.p);
  m.b.build_return();
  auto &s = m.ssa();
  EXPECT_TRUE(m.valid());
  const auto def_p = s.access_of(st_p);
  const auto def_r = s.access_of(st_r);
  EXPECT_EQ(s[def_p].kind, MemorySSA::Kind::Def);
  EXPECT_EQ(s[def_p].defining, MemorySSA::LiveOnEntry);
  EXPECT_EQ(s[def_r].defining, def_p);
  EXPECT_EQ(s.defining_access(ld_q), def_p);
  EXPECT_EQ(s.defining_access(ld_p2), def_r);
  // p and q are noalias, so the store to p is not a clobber of q
  EXPECT_EQ(m.clobber(ld_q), MemorySSA::LiveOnEntry);
  EXPECT_EQ(m.clobber(ld_p), def_p);
  // the second store to p clobbers
  EXPECT_EQ(m.clobber(ld_p2), def_r);
}

TEST(MemorySSA, NoMemoryNoAccess) {
  MemSSAModule m;
  auto sum = m.b.build_int_add(m.c32(1), m.c32(2)).as_instr();
  m.b.build_return();
  EXPECT_EQ(m.ssa().access_of(sum), MemorySSA::None);
  EXPECT_EQ(m.ssa().n_accesses(), 1U);
}

TEST(MemorySSA, FenceIsDefAndBlocks) {
  MemSSAModule m;
  auto ld1 = m.load_i(m.p);
  auto fence = m.b.build_fence(foptim::fir::Ordering::Seq_Cst).as_instr();
  auto ld2 = m.load_i(m.p);
  auto vol = m.b.build_load(m.i32, m.q, false, true).as_instr();
  auto ld3 = m.load_i(m.q);
  m.b.build_return();
  auto &s = m.ssa();
  EXPECT_EQ(s[s.access_of(fence)].kind, MemorySSA::Kind::Def);
  EXPECT_EQ(s[s.access_of(vol)].kind, MemorySSA::Kind::Def);
  EXPECT_EQ(m.clobber(ld1), MemorySSA::LiveOnEntry);
  EXPECT_EQ(m.clobber(ld2), s.access_of(fence));
  EXPECT_EQ(m.clobber(ld3), s.access_of(vol));
  EXPECT_TRUE(m.valid());
}

TEST(MemorySSA, DiamondPhi) {
  MemSSAModule m;
  m.diamond([&] { m.store(m.p); }, [&] { m.store(m.q); });
  auto ld_p = m.load_i(m.p);
  auto ld_q = m.load_i(m.q);
  auto ld_r = m.load_i(m.r);
  m.b.build_return();
  auto &s = m.ssa();
  EXPECT_TRUE(m.valid());
  const foptim::u32 phi = s.phi_of(3);
  ASSERT_NE(phi, MemorySSA::None);
  EXPECT_EQ(s.n_operands(phi), 2U);
  EXPECT_EQ(s.defining_access(ld_p), phi);
  // each arm writes one of the two objects, the other path reaches the entry
  EXPECT_EQ(m.clobber(ld_p), phi);
  EXPECT_EQ(m.clobber(ld_q), phi);
  // r is neither p nor q
  EXPECT_EQ(m.clobber(ld_r), MemorySSA::LiveOnEntry);
}

// both paths lead to the same store: the phi is skipped
TEST(MemorySSA, WalkSkipsPhiWithOneClobber) {
  MemSSAModule m;
  auto st = m.store(m.p);
  m.diamond([&] { m.store(m.q); });
  auto ld = m.load_i(m.p);
  m.b.build_return();
  auto &s = m.ssa();
  EXPECT_NE(s.phi_of(3), MemorySSA::None);
  EXPECT_EQ(m.clobber(ld), s.access_of(st));
}

// a phi that merges one state is not created
// A phi that is reached over two paths is answered once. The phi M below is
// the answer of the path through V, the path through K ends in the phi X, so
// the answer for Q is Q itself. Skipping M on the second visit gave X, which
// the path over V does not pass.
TEST(MemorySSA, WalkReachesPhiOverTwoPaths) {
  MemSSAModule m;
  auto t = m.b.append_bb();
  auto f = m.b.append_bb();
  auto j = m.b.append_bb();
  auto u = m.b.append_bb();
  auto v = m.b.append_bb();
  auto k = m.b.append_bb();
  auto q = m.b.append_bb();
  m.b.build_cond_branch(m.cond, t, f);
  m.b.at_end(t);
  m.store(m.p, 1);
  m.b.build_branch(j);
  m.b.at_end(f);
  m.b.build_branch(j);
  m.b.at_end(j);
  m.b.build_cond_branch(m.cond, u, v);
  m.b.at_end(u);
  m.store(m.p, 2);
  m.b.build_branch(k);
  m.b.at_end(v);
  m.b.build_cond_branch(m.cond, k, q);
  m.b.at_end(k);
  m.b.build_branch(q);
  m.b.at_end(q);
  auto ld = m.load_i(m.p);
  m.b.build_return();
  auto &s = m.ssa();
  EXPECT_TRUE(m.valid());
  EXPECT_EQ(m.clobber(ld), s.defining_access(ld));
  EXPECT_EQ(s[m.clobber(ld)].kind, MemorySSA::Kind::Phi);
}

TEST(MemorySSA, NoTrivialPhi) {
  MemSSAModule m;
  m.store(m.p);
  m.diamond();
  auto ld = m.load_i(m.p);
  m.b.build_return();
  auto &s = m.ssa();
  EXPECT_EQ(s.phi_of(3), MemorySSA::None);
  EXPECT_EQ(s.defining_access(ld), 1U);
  EXPECT_TRUE(m.valid());
}

TEST(MemorySSA, LoopPhiAndBackEdge) {
  MemSSAModule m;
  auto header = m.b.append_bb();
  auto exit = m.b.append_bb();
  m.b.build_branch(header);
  m.b.at_end(header);
  auto ld_p = m.load_i(m.p);
  auto ld_r = m.load_i(m.r);
  auto st_q = m.store(m.q);
  m.b.build_cond_branch(m.cond, header, exit);
  m.b.at_end(exit);
  auto ld_q = m.load_i(m.q);
  m.b.build_return();
  auto &s = m.ssa();
  EXPECT_TRUE(m.valid());
  const foptim::u32 phi = s.phi_of(1);
  ASSERT_NE(phi, MemorySSA::None);
  EXPECT_EQ(s.operand(phi, 0), MemorySSA::LiveOnEntry);
  EXPECT_EQ(s.operand(phi, 1), s.access_of(st_q));
  EXPECT_EQ(s.defining_access(ld_p), phi);
  // the store to q in the previous iteration does not reach p (noalias)
  EXPECT_EQ(m.clobber(ld_p), MemorySSA::LiveOnEntry);
  EXPECT_EQ(m.clobber(ld_r), MemorySSA::LiveOnEntry);
  EXPECT_EQ(m.clobber(ld_q), s.access_of(st_q));
}

// the entry block is a loop header: the phi also has the incoming edge of the
// function
TEST(MemorySSA, EntryWithBackEdge) {
  MemSSAModule m;
  auto exit = m.b.append_bb();
  auto ld = m.load_i(m.r);
  auto st = m.store(m.r);
  m.b.build_cond_branch(m.cond, m.f.func->get_entry(), exit);
  m.b.at_end(exit);
  m.b.build_return();
  auto &s = m.ssa();
  const foptim::u32 phi = s.phi_of(0);
  ASSERT_NE(phi, MemorySSA::None);
  EXPECT_EQ(s.n_operands(phi), 2U);
  EXPECT_EQ(s.operand(phi, 0), s.access_of(st));
  EXPECT_EQ(s.operand(phi, 1), MemorySSA::LiveOnEntry);
  EXPECT_EQ(s.defining_access(ld), phi);
  EXPECT_TRUE(m.valid());
}

TEST(MemorySSA, QueryCapIsConservative) {
  MemSSAModule m;
  // many stores to q, the load of p skips all of them (noalias)
  for (int i = 0; i < 10; i++) {
    m.store(m.q);
  }
  auto ld = m.load_i(m.p);
  m.b.build_return();
  EXPECT_EQ(m.clobber(ld), MemorySSA::LiveOnEntry);
  // gives up early and answers with the access it stopped at
  const foptim::u32 capped = m.clobber(ld, 4);
  EXPECT_NE(capped, MemorySSA::LiveOnEntry);
  EXPECT_EQ(m.ssa()[capped].kind, MemorySSA::Kind::Def);
}

TEST(MemorySSA, ClobberedBetween) {
  MemSSAModule m;
  auto st_q = m.store(m.q);
  auto ld1 = m.load_i(m.p);
  auto st_r = m.store(m.p, 2);
  auto ld2 = m.load_i(m.p);
  m.b.build_return();
  auto &s = m.ssa();
  AliasQuery aq{AnalysisManager::provenance(*m.f.func)};
  auto &info = AnalysisManager::mem_accesses(*m.f.func);
  MemorySSAWalker w{s, info, aq};
  const auto *la = info.find(ld1);
  const foptim::u32 d1 = s.defining_access(ld1);
  const foptim::u32 d2 = s.defining_access(ld2);
  EXPECT_EQ(d1, s.access_of(st_q));
  EXPECT_EQ(d2, s.access_of(st_r));
  // between the two loads of p only the second store to p happens
  EXPECT_TRUE(w.clobbered_between(d1, d2, la->loc));
  EXPECT_FALSE(w.clobbered_between(d1, d1, la->loc));
  // from the entry to the store to q: q does not alias p
  EXPECT_FALSE(w.clobbered_between(MemorySSA::LiveOnEntry, d1, la->loc));
}

TEST(MemorySSA, ClobberedBetweenOnlyDominatingFrom) {
  MemSSAModule m;
  foptim::fir::Instr st;
  m.diamond([&] { st = m.store(m.q); });
  auto ld = m.load_i(m.p);
  m.b.build_return();
  auto &s = m.ssa();
  AliasQuery aq{AnalysisManager::provenance(*m.f.func)};
  auto &info = AnalysisManager::mem_accesses(*m.f.func);
  MemorySSAWalker w{s, info, aq};
  // the store in then does not dominate the join, a path misses it
  EXPECT_TRUE(w.clobbered_between(s.access_of(st), s.defining_access(ld),
                                  info.find(ld)->loc));
}

TEST(MemorySSA, CacheDropsOnChange) {
  MemSSAModule m;
  m.store(m.p);
  m.b.build_return();
  auto &first = AnalysisManager::memory_ssa(*m.f.func);
  EXPECT_EQ(&first, &AnalysisManager::memory_ssa(*m.f.func));
  AnalysisManager::invalidate(*m.f.func, PreservedAnalysis::all());
  EXPECT_EQ(&first, &AnalysisManager::memory_ssa(*m.f.func));
  AnalysisManager::invalidate(*m.f.func, PreservedAnalysis::cfg_only());
  EXPECT_FALSE(m.f.func->analysis_cache->memory_ssa);
}

TEST(MemorySSA, PrintsPhisAndClobbers) {
  MemSSAModule m;
  m.diamond([&] { m.store(m.p); });
  m.load_i(m.p);
  m.b.build_return();
  auto &s = m.ssa();
  const auto text = s.to_string(AnalysisManager::cfg(*m.f.func));
  EXPECT_NE(text.find("MemoryPhi"), std::string::npos);
  EXPECT_NE(text.find("MemoryDef(0)"), std::string::npos);
  EXPECT_NE(text.find("LiveOnEntry"), std::string::npos);
}
}  // namespace
