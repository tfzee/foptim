#include <gtest/gtest.h>

#include <climits>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/mem/mem_access.hpp"

namespace {
using foptim::fir::ValueR;
using foptim::optim::AnalysisManager;
using foptim::optim::decompose_ptr;
using foptim::optim::MemAccess;
using foptim::optim::MemLoc;
using foptim::optim::PreservedAnalysis;

// f(p, q, i, j, k i32), blocks and results are built per test
struct MemAccessModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR i64{ctx.data->get_int_type(64)};
  foptim::fir::TypeR i32{ctx.data->get_int_type(32)};
  foptim::fir::TypeR ptr{ctx.data->get_ptr_type()};
  foptim::fir::FunctionR f;
  foptim::fir::Builder b;
  ValueR p, q, i, j, k;

  ValueR c(foptim::i64 v) {
    return ValueR{ctx.data->get_constant_value(v, i64)};
  }
  ValueR add(ValueR base, foptim::i64 off) {
    return b.build_ptr_add(base, c(off));
  }
  ValueR add(ValueR base, ValueR idx) { return b.build_ptr_add(base, idx); }
  ValueR scaled(ValueR idx, foptim::i64 s) {
    return b.build_int_mul(idx, c(s));
  }

  MemAccessModule()
      : f(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_void_type(),
                                       {ptr, ptr, i64, i64, i32}))),
        b(f.builder()) {
    auto entry = f.func->get_entry();
    b.at_end(entry);
    p = ValueR{entry->args[0]};
    q = ValueR{entry->args[1]};
    i = ValueR{entry->args[2]};
    j = ValueR{entry->args[3]};
    k = ValueR{entry->args[4]};
  }
};

TEST(MemAccess, PlainBase) {
  MemAccessModule m;
  auto s = decompose_ptr(m.p);
  EXPECT_EQ(s.base, m.p);
  EXPECT_EQ(s.offset, 0);
  EXPECT_FALSE(s.has_terms());
  EXPECT_EQ(s.anchor, m.p);
  EXPECT_EQ(s.anchor_offset, 0);
}

TEST(MemAccess, NestedConstants) {
  MemAccessModule m;
  auto inner = m.add(m.p, 8);
  auto s = decompose_ptr(m.add(inner, -24));
  EXPECT_EQ(s.base, m.p);
  EXPECT_EQ(s.offset, -16);
  EXPECT_EQ(s.anchor, m.p);
  EXPECT_EQ(s.anchor_offset, -16);
  EXPECT_FALSE(s.has_terms());
}

TEST(MemAccess, ScaledIndex) {
  MemAccessModule m;
  auto s = decompose_ptr(m.add(m.p, m.scaled(m.i, 4)));
  EXPECT_EQ(s.base, m.p);
  EXPECT_EQ(s.offset, 0);
  ASSERT_EQ(s.terms().size(), 1U);
  EXPECT_EQ(s.terms()[0].index, m.i);
  EXPECT_EQ(s.terms()[0].scale, 4);
}

TEST(MemAccess, ShiftedIndex) {
  MemAccessModule m;
  auto s = decompose_ptr(m.add(m.p, m.b.build_shl(m.i, m.c(3))));
  ASSERT_EQ(s.terms().size(), 1U);
  EXPECT_EQ(s.terms()[0].index, m.i);
  EXPECT_EQ(s.terms()[0].scale, 8);
}

TEST(MemAccess, ConstantInsideIndex) {
  MemAccessModule m;
  // p + (i * 4 + 8) is the same address as (p + i * 4) + 8
  auto idx = m.b.build_int_add(m.scaled(m.i, 4), m.c(8));
  auto one = m.add(m.p, idx);
  auto two = m.add(m.add(m.p, m.scaled(m.i, 4)), 8);
  auto a = decompose_ptr(one);
  auto b = decompose_ptr(two);
  EXPECT_EQ(a.base, m.p);
  EXPECT_EQ(a.offset, 8);
  EXPECT_TRUE(a == b);
  // the anchor is a value that exists: the pointer with the variable part
  EXPECT_EQ(a.anchor, one);
  EXPECT_EQ(a.anchor_offset, 0);
  EXPECT_EQ(b.anchor, two.as_instr()->args[0]);
  EXPECT_EQ(b.anchor_offset, 8);
}

TEST(MemAccess, SubtractedIndexCancels) {
  MemAccessModule m;
  auto x = m.add(m.add(m.p, m.i), m.b.build_int_sub(m.c(0), m.i));
  auto s = decompose_ptr(x);
  EXPECT_EQ(s.base, m.p);
  EXPECT_FALSE(s.has_terms());
  EXPECT_EQ(s.offset, 0);
}

TEST(MemAccess, SameIndexTwiceMerges) {
  MemAccessModule m;
  auto s =
      decompose_ptr(m.add(m.add(m.p, m.scaled(m.i, 4)), m.scaled(m.i, 12)));
  ASSERT_EQ(s.terms().size(), 1U);
  EXPECT_EQ(s.terms()[0].scale, 16);
}

TEST(MemAccess, TermsAreAnUnorderedSet) {
  MemAccessModule m;
  auto a = decompose_ptr(m.add(m.add(m.p, m.i), m.j));
  auto b = decompose_ptr(m.add(m.add(m.p, m.j), m.i));
  EXPECT_TRUE(a.same_base(b));
  auto c = decompose_ptr(m.add(m.add(m.p, m.i), m.scaled(m.j, 2)));
  EXPECT_FALSE(a.same_base(c));
}

TEST(MemAccess, NarrowIndexIsOpaque) {
  MemAccessModule m;
  // the add happens in 32 bit, it must not be split into two terms
  auto sum = m.b.build_int_add(m.k, m.k);
  auto s = decompose_ptr(m.add(m.p, sum));
  ASSERT_EQ(s.terms().size(), 1U);
  EXPECT_EQ(s.terms()[0].index, sum);
  EXPECT_EQ(s.terms()[0].scale, 1);
}

TEST(MemAccess, BlockArgumentIsABase) {
  MemAccessModule m;
  auto join = m.b.append_bb();
  join.add_arg(m.ctx.data->storage.insert_bb_arg(join, m.ptr));
  auto br = m.b.build_branch(join);
  br.add_bb_arg(0, m.p);
  m.b.at_end(join);
  auto phi = ValueR{join->args[0]};
  auto s = decompose_ptr(m.add(phi, 16));
  EXPECT_EQ(s.base, phi);
  EXPECT_EQ(s.offset, 16);
  m.b.build_return();
}

TEST(MemAccess, OffsetOverflowStopsTheChain) {
  MemAccessModule m;
  auto big = m.add(m.p, static_cast<foptim::i64>(INT64_MAX));
  auto s = decompose_ptr(m.add(big, 8));
  EXPECT_EQ(s.base, big);
  EXPECT_EQ(s.offset, 8);
}

TEST(MemAccess, TooManyTermsStopTheChain) {
  MemAccessModule m;
  auto x = m.add(m.add(m.add(m.add(m.p, m.i), m.j), m.scaled(m.i, 8)),
                 m.scaled(m.j, 4));
  auto y = m.add(x, m.b.build_int_add(m.i, m.j));
  auto s = decompose_ptr(y);
  EXPECT_LE(s.terms().size(), 3U);
  EXPECT_TRUE(s.base == m.p || s.base.is_instr());
}

TEST(MemAccess, Overlaps) {
  MemAccessModule m;
  auto loc = [&](ValueR addr, foptim::u32 size) {
    return MemLoc{.ptr = decompose_ptr(addr), .size = size};
  };
  auto a = m.add(m.p, m.scaled(m.i, 4));
  EXPECT_EQ(loc(a, 4).overlaps(loc(m.add(a, 4), 4)), false);
  EXPECT_EQ(loc(a, 8).overlaps(loc(m.add(a, 4), 4)), true);
  EXPECT_EQ(loc(m.p, 4).overlaps(loc(m.add(m.p, -4), 4)), false);
  EXPECT_EQ(loc(m.p, 4).overlaps(loc(m.add(m.p, -3), 4)), true);
  // other variable part or base: only an alias analysis can tell
  EXPECT_EQ(loc(a, 4).overlaps(loc(m.add(m.p, m.scaled(m.j, 4)), 4)),
            std::nullopt);
  EXPECT_EQ(loc(m.p, 4).overlaps(loc(m.q, 4)), std::nullopt);
  EXPECT_EQ(loc(m.p, 0).overlaps(loc(m.p, 4)), std::nullopt);
}

TEST(MemAccess, ClassifyAndCache) {
  MemAccessModule m;
  auto x = m.add(m.p, m.scaled(m.i, 4));
  auto ld = m.b.build_load(m.i32, x, false, false);
  auto vld = m.b.build_load(m.i32, m.q, false, true);
  auto st = m.b.build_store(m.add(x, 4), ld, false, false);
  m.b.build_return();

  auto a = foptim::optim::classify_access(ld.as_instr());
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->kind, MemAccess::Kind::Load);
  EXPECT_EQ(a->loc.size, 4U);
  EXPECT_FALSE(a->ordered);
  auto v = foptim::optim::classify_access(vld.as_instr());
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->kind, MemAccess::Kind::BarrierRW);
  EXPECT_TRUE(v->ordered);
  EXPECT_FALSE(foptim::optim::classify_access(x.as_instr()).has_value());

  auto &info = AnalysisManager::mem_accesses(*m.f.func);
  ASSERT_EQ(info.accesses().size(), 3U);
  const auto *ls = info.find(st.as_instr());
  ASSERT_NE(ls, nullptr);
  EXPECT_EQ(ls->kind, MemAccess::Kind::Store);
  EXPECT_EQ(ls->loc.ptr.offset, 4);
  EXPECT_EQ(&info, &AnalysisManager::mem_accesses(*m.f.func));

  // instructions may have changed, only all() keeps it
  AnalysisManager::invalidate(*m.f.func, PreservedAnalysis::all());
  EXPECT_NE(m.f.func->analysis_cache->mem_accesses, nullptr);
  AnalysisManager::invalidate(*m.f.func, PreservedAnalysis::cfg_only());
  EXPECT_EQ(m.f.func->analysis_cache->mem_accesses, nullptr);
}
}  // namespace
