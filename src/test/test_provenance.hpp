#pragma once
#include <gtest/gtest.h>

#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/mem/alias_sets.hpp"
#include "optim/analysis/mem/provenance.hpp"
#include "test_alias.hpp"

namespace {
using foptim::optim::AliasSetTable;
using foptim::optim::AnalysisManager;
using foptim::optim::PreservedAnalysis;
using foptim::optim::Provenance;
using foptim::optim::SetId;
using T = AliasSetTable;

TEST(AliasSets, RootRelations) {
  const auto &r = AliasSetTable::roots();
  const SetId disjoint_roots[] = {T::Null, T::Stack, T::Globals, T::Heap};
  for (auto a : disjoint_roots) {
    for (auto b : disjoint_roots) {
      EXPECT_EQ(r.disjoint(a, b), a != b) << a << " " << b;
    }
  }
  for (SetId s = 0; s < T::NumRoots; s++) {
    EXPECT_FALSE(r.disjoint(s, T::Unknown));
    EXPECT_FALSE(r.disjoint(s, s));
    EXPECT_TRUE(r.subset(s, T::Unknown));
    EXPECT_TRUE(r.subset(s, s));
  }
  EXPECT_TRUE(r.subset(T::Null, T::Foreign));
  EXPECT_TRUE(r.subset(T::Globals, T::Foreign));
  EXPECT_TRUE(r.subset(T::ReadOnlyGlobals, T::Globals));
  EXPECT_TRUE(r.subset(T::ReadOnlyGlobals, T::Foreign));
  EXPECT_FALSE(r.subset(T::Globals, T::ReadOnlyGlobals));
  EXPECT_FALSE(r.subset(T::Stack, T::Foreign));
  // a subset inherits the disjointness of its parents
  EXPECT_TRUE(r.disjoint(T::ReadOnlyGlobals, T::Heap));
  EXPECT_TRUE(r.disjoint(T::ReadOnlyGlobals, T::Stack));
  EXPECT_TRUE(r.disjoint(T::Incoming, T::Stack));
  EXPECT_TRUE(r.disjoint(T::Incoming, T::Heap));
  EXPECT_FALSE(r.disjoint(T::Incoming, T::Globals));
  EXPECT_FALSE(r.disjoint(T::Incoming, T::Null));
  EXPECT_TRUE(r.disjoint(T::IncomingNonNull, T::Null));
  EXPECT_FALSE(r.disjoint(T::Foreign, T::Null));
  EXPECT_FALSE(r.disjoint(T::Foreign, T::Stack));
  for (auto other :
       {T::Incoming, T::IncomingNonNull, T::Stack, T::Globals, T::Heap}) {
    EXPECT_TRUE(r.disjoint(T::Restrict, other));
  }
  EXPECT_FALSE(r.disjoint(T::Restrict, T::Null));
  EXPECT_FALSE(r.disjoint(T::Restrict, T::Foreign));
}

TEST(AliasSets, ObjectsOfOneRootAreDisjoint) {
  AliasSetTable t{AliasSetTable::roots()};
  auto a = t.create_object(T::Stack);
  auto b = t.create_object(T::Stack);
  auto g = t.create_object(T::Globals);
  EXPECT_TRUE(t.disjoint(a, b));
  EXPECT_TRUE(t.disjoint(b, a));
  EXPECT_TRUE(t.disjoint(a, g));
  EXPECT_FALSE(t.disjoint(a, a));
  EXPECT_TRUE(t.subset(a, T::Stack));
  EXPECT_FALSE(t.subset(a, b));
  EXPECT_FALSE(t.disjoint(a, T::Stack));
  EXPECT_FALSE(t.disjoint(a, T::Unknown));
  // a part of an object is disjoint from the other objects too
  const SetId parent[] = {a};
  auto part = t.create(parent);
  EXPECT_TRUE(t.subset(part, a));
  EXPECT_TRUE(t.disjoint(part, b));
  EXPECT_FALSE(t.disjoint(part, a));
  // the overlay does not write the shared roots
  EXPECT_EQ(AliasSetTable::roots().size(), T::NumRoots);
}

TEST(AliasSets, DeclaredDisjointInOverlay) {
  AliasSetTable t{AliasSetTable::roots()};
  auto obj = t.create_object(T::Stack);
  EXPECT_FALSE(t.disjoint(obj, T::Foreign));
  t.declare_disjoint(obj, T::Foreign);
  EXPECT_TRUE(t.disjoint(obj, T::Foreign));
  EXPECT_TRUE(t.disjoint(T::Foreign, obj));
  // everything below Foreign is disjoint from the object
  EXPECT_TRUE(t.disjoint(obj, T::Incoming));
  EXPECT_TRUE(t.disjoint(obj, T::Null));
  EXPECT_TRUE(t.disjoint(T::Heap, obj));
  EXPECT_FALSE(t.disjoint(obj, T::Unknown));
  // the roots are untouched
  EXPECT_FALSE(AliasSetTable::roots().disjoint(T::Stack, T::Foreign));
}

TEST(AliasSets, Unions) {
  AliasSetTable t{AliasSetTable::roots()};
  auto a = t.create_object(T::Stack);
  auto b = t.create_object(T::Stack);
  auto c = t.create_object(T::Stack);
  auto h = t.create_object(T::Heap);
  auto ab = t.union_of(a, b);
  EXPECT_EQ(ab, t.union_of(b, a));
  EXPECT_EQ(t.union_of(a, a), a);
  EXPECT_TRUE(t.is_union(ab));
  EXPECT_TRUE(t.subset(a, ab));
  EXPECT_TRUE(t.subset(b, ab));
  EXPECT_FALSE(t.subset(c, ab));
  EXPECT_FALSE(t.subset(ab, a));
  EXPECT_TRUE(t.subset(ab, T::Stack));
  EXPECT_TRUE(t.subset(ab, ab));
  EXPECT_FALSE(t.subset(ab, T::Heap));
  // disjoint only if every part is
  EXPECT_TRUE(t.disjoint(ab, c));
  EXPECT_TRUE(t.disjoint(ab, h));
  EXPECT_FALSE(t.disjoint(ab, a));
  EXPECT_FALSE(t.disjoint(ab, T::Stack));
  // flattening and dropping parts that are inside another part
  auto abc = t.union_of(ab, c);
  EXPECT_EQ(abc, t.union_of(a, t.union_of(b, c)));
  EXPECT_EQ(t.union_of(abc, a), abc);
  EXPECT_TRUE(t.subset(ab, abc));
  EXPECT_FALSE(t.subset(abc, ab));
  EXPECT_EQ(t.union_of(a, T::Stack), T::Stack);
  EXPECT_EQ(t.union_of(a, T::Unknown), T::Unknown);
  EXPECT_EQ(t.union_of(ab, T::Stack), T::Stack);
  // a union of sets of different roots
  auto ah = t.union_of(a, h);
  EXPECT_FALSE(t.subset(ah, T::Stack));
  EXPECT_TRUE(t.disjoint(ah, T::Null));
  EXPECT_TRUE(t.disjoint(ah, b));
  EXPECT_FALSE(t.disjoint(ah, a));
  EXPECT_FALSE(t.disjoint(ah, h));
  // too many parts are widened to the deepest common ancestor
  auto d = t.create_object(T::Stack);
  auto e = t.create_object(T::Stack);
  auto big = t.union_of(t.union_of(abc, d), e);
  EXPECT_EQ(big, T::Stack);
  // unions of the roots are found again by an overlay of the overlay
  AliasSetTable t2{t};
  EXPECT_EQ(t2.union_of(a, b), ab);
  auto f = t2.create_object(T::Stack);
  EXPECT_TRUE(t2.disjoint(f, ab));
  EXPECT_TRUE(t2.subset(ab, T::Stack));
  EXPECT_TRUE(t2.disjoint(t2.union_of(f, a), b));
}

TEST(AliasSets, MembershipsIntersect) {
  AliasSetTable t{AliasSetTable::roots()};
  auto a = t.create_object(T::Stack);
  auto b = t.create_object(T::Stack);
  // an access that is in several sets lies in their intersection
  const SetId in_a_and_unknown[] = {T::Unknown, a};
  const SetId in_b[] = {b};
  const SetId unknown_only[] = {T::Unknown};
  EXPECT_TRUE(t.disjoint_any(in_a_and_unknown, in_b));
  EXPECT_FALSE(t.disjoint_any(unknown_only, in_b));
}

TEST(Provenance, Roots) {
  AliasModule m;
  auto alloca_plus8 = m.add(m.alloca1, 8);
  Provenance p{*m.f.func};
  const auto &t = p.table();
  EXPECT_TRUE(p.definitely_null(m.null));
  EXPECT_TRUE(p.non_null(m.alloca1));
  EXPECT_TRUE(p.non_null(m.g1));
  EXPECT_TRUE(p.non_null(m.d));
  EXPECT_FALSE(p.non_null(m.a));
  EXPECT_FALSE(p.non_null(m.loaded_a));
  EXPECT_FALSE(p.non_null(m.call_res));
  EXPECT_FALSE(p.non_null(m.null));
  EXPECT_FALSE(p.definitely_null(m.a));
  EXPECT_TRUE(p.is_stack(m.alloca1));
  EXPECT_TRUE(p.is_stack(alloca_plus8));
  EXPECT_FALSE(p.is_stack(m.a));
  EXPECT_TRUE(p.is_global(m.g1));
  EXPECT_FALSE(p.is_global(m.alloca1));
  // alloca1 is an operand of a branch argument, alloca2 never leaves
  EXPECT_TRUE(p.escapes(m.alloca1));
  EXPECT_FALSE(p.escapes(m.alloca2));
  EXPECT_FALSE(p.escapes(m.a));
  EXPECT_NE(p.set_of(m.alloca1), p.set_of(m.alloca2));
  EXPECT_NE(p.set_of(m.g1), p.set_of(m.g2));
  EXPECT_EQ(p.set_of(m.n), T::Unknown);
  EXPECT_FALSE(t.disjoint(p.set_of(m.n), T::Stack));
}

TEST(Provenance, Disjointness) {
  AliasModule m;
  Provenance p{*m.f.func};
  // objects
  EXPECT_TRUE(p.disjoint(m.alloca1, m.alloca2));
  EXPECT_FALSE(p.disjoint(m.alloca1, m.alloca1));
  EXPECT_TRUE(p.disjoint(m.g1, m.g2));
  EXPECT_TRUE(p.disjoint(m.alloca1, m.g1));
  EXPECT_TRUE(p.disjoint(m.null, m.alloca1));
  EXPECT_TRUE(p.disjoint(m.null, m.g1));
  // arguments
  EXPECT_FALSE(p.disjoint(m.a, m.bb));
  EXPECT_FALSE(p.disjoint(m.a, m.g1));
  EXPECT_TRUE(p.disjoint(m.a, m.alloca1));
  EXPECT_TRUE(p.disjoint(m.a, m.alloca2));
  EXPECT_TRUE(p.disjoint(m.c, m.a));
  EXPECT_TRUE(p.disjoint(m.c, m.d));
  EXPECT_FALSE(p.non_null(m.c));
  EXPECT_TRUE(p.disjoint(m.c, m.g1));
  EXPECT_TRUE(p.disjoint(m.c, m.alloca1));
  EXPECT_FALSE(p.disjoint(m.c, m.c));
  // memory behind other pointers may be reached by loaded ones, except for
  // the allocas that never escape
  EXPECT_FALSE(p.disjoint(m.loaded_a, m.a));
  EXPECT_FALSE(p.disjoint(m.loaded_a, m.g1));
  EXPECT_FALSE(p.disjoint(m.loaded_a, m.c));
  EXPECT_FALSE(p.disjoint(m.loaded_a, m.loaded_b));
  EXPECT_FALSE(p.disjoint(m.loaded_a, m.alloca1));
  EXPECT_TRUE(p.disjoint(m.loaded_a, m.alloca2));
  EXPECT_TRUE(p.disjoint(m.call_res, m.alloca2));
  EXPECT_FALSE(p.disjoint(m.call_res, m.alloca1));
  // inttoptr could be anything
  EXPECT_FALSE(p.disjoint(m.int2ptr, m.alloca2));
  EXPECT_FALSE(p.disjoint(m.int2ptr, m.a));
  EXPECT_FALSE(p.disjoint(m.int2ptr, m.g1));
  // the block argument only gets alloca1
  EXPECT_EQ(p.set_of(m.phi), p.set_of(m.alloca1));
  EXPECT_TRUE(p.disjoint(m.phi, m.alloca2));
}

TEST(Provenance, PointerArithmetic) {
  AliasModule m;
  auto plus8 = m.add(m.alloca2, 8);
  auto idx_plain = m.b.build_ptr_add(m.alloca2, m.n);
  auto idx_inb = m.b.build_ptr_add(m.alloca2, m.n, true);
  auto null8 = m.add(m.null, 8);
  auto d_plain = m.add(m.d, 8);
  auto d_inb = m.b.build_ptr_add(m.d, m.k(8), true);
  auto d_var_inb = m.b.build_ptr_add(m.d, m.n, true);
  Provenance p{*m.f.func};
  EXPECT_EQ(p.set_of(plus8), p.set_of(m.alloca2));
  // a pointer computed from another one stays in its object, inbounds or not
  EXPECT_EQ(p.set_of(idx_plain), p.set_of(m.alloca2));
  EXPECT_EQ(p.set_of(idx_inb), p.set_of(m.alloca2));
  EXPECT_TRUE(p.disjoint(idx_plain, m.a));
  EXPECT_TRUE(p.disjoint(idx_inb, m.a));
  // the offset from the unknown base does not make it an object
  EXPECT_FALSE(p.disjoint(m.add(m.loaded_a, 8), m.add(m.loaded_b, 16)));
  EXPECT_FALSE(p.disjoint(m.add(m.a, 8), m.add(m.bb, 16)));
  // null plus something is not null
  EXPECT_FALSE(p.definitely_null(null8));
  // nonnull survives an inbounds addition only
  EXPECT_TRUE(p.non_null(d_inb));
  EXPECT_TRUE(p.non_null(d_var_inb));
  EXPECT_FALSE(p.non_null(d_plain));
  EXPECT_TRUE(p.non_null(plus8));
}

TEST(Provenance, SelectIsAUnion) {
  AliasModule m;
  auto sel_local = m.b.build_select(m.ptr, m.cond, m.alloca1, m.alloca2);
  auto sel_mixed = m.b.build_select(m.ptr, m.cond, m.alloca2, m.g1);
  auto sel_arg = m.b.build_select(m.ptr, m.cond, m.alloca2, m.a);
  auto sel_nn = m.b.build_select(m.ptr, m.cond, m.alloca2, m.d);
  auto sel_nul = m.b.build_select(m.ptr, m.cond, m.alloca2, m.null);
  Provenance p{*m.f.func};
  EXPECT_TRUE(p.table().is_union(p.set_of(sel_local)));
  EXPECT_TRUE(p.is_stack(sel_local));
  EXPECT_FALSE(p.disjoint(sel_local, m.alloca1));
  EXPECT_FALSE(p.disjoint(sel_local, m.alloca2));
  EXPECT_TRUE(p.disjoint(sel_local, m.a));
  EXPECT_TRUE(p.disjoint(sel_local, m.g1));
  EXPECT_FALSE(p.disjoint(sel_mixed, m.g1));
  EXPECT_TRUE(p.disjoint(sel_mixed, m.alloca1));
  EXPECT_TRUE(p.disjoint(sel_mixed, m.g2));
  EXPECT_FALSE(p.is_stack(sel_mixed));
  EXPECT_FALSE(p.disjoint(sel_arg, m.alloca2));
  EXPECT_TRUE(p.disjoint(sel_arg, m.alloca1));
  EXPECT_TRUE(p.non_null(sel_nn));
  EXPECT_FALSE(p.non_null(sel_nul));
  EXPECT_FALSE(p.non_null(sel_arg));
}

// p = phi(a, p + 4), the sets of the loop carried value have to settle
TEST(Provenance, LoopCarriedPointer) {
  AliasModule m;
  auto header = m.b.append_bb();
  header.add_arg(m.ctx.data->storage.insert_bb_arg(header, m.ptr));
  // the entry already ends in a branch to join, build the loop elsewhere
  auto loop_entry = m.b.append_bb();
  m.b.at_end(loop_entry);
  auto br = m.b.build_branch(header);
  br.add_bb_arg(0, m.a);
  m.b.at_end(header);
  auto p = foptim::fir::ValueR{header->args[0]};
  auto next = m.add(p, 4);
  auto sel = m.b.build_select(m.ptr, m.cond, next, m.alloca2);
  auto back = m.b.build_cond_branch(m.cond, header, header);
  back.add_bb_arg(0, next);
  back.add_bb_arg(1, sel);
  Provenance prov{*m.f.func};
  // a and the alloca flow into the header argument
  EXPECT_FALSE(prov.disjoint(p, m.a));
  EXPECT_FALSE(prov.disjoint(p, m.alloca2));
  EXPECT_TRUE(prov.disjoint(p, m.alloca1));
  // an argument may point to a global
  EXPECT_FALSE(prov.disjoint(p, m.g1));
}

TEST(Provenance, AllocaThatKeepsAPointerToItself) {
  AliasModule m;
  auto slot = m.b.build_alloca(m.k(16));
  auto other = m.b.build_alloca(m.k(16));
  // slot stores a pointer into itself, std::string with its inline buffer
  m.b.build_store(slot, m.add(slot, 8), false, false);
  auto back = m.b.build_load(m.ptr, slot, false, false);
  auto plain = m.b.build_load(m.ptr, other, false, false);
  Provenance p{*m.f.func};
  EXPECT_FALSE(p.escapes(slot));
  EXPECT_FALSE(p.escapes(other));
  // what is read back out of slot may be the buffer, or what someone else put
  // there
  EXPECT_FALSE(p.disjoint(back, slot));
  EXPECT_FALSE(p.disjoint(back, m.a));
  EXPECT_TRUE(p.disjoint(back, other));
  // a load from memory that holds no pointer to an alloca
  EXPECT_FALSE(p.disjoint(plain, m.a));
  EXPECT_TRUE(p.disjoint(plain, slot));
  EXPECT_TRUE(p.disjoint(plain, other));
}

TEST(Provenance, CacheDropsOnChange) {
  AliasModule m;
  auto &p = AnalysisManager::provenance(*m.f.func);
  EXPECT_EQ(&p, &AnalysisManager::provenance(*m.f.func));
  AnalysisManager::invalidate(*m.f.func, PreservedAnalysis::all());
  EXPECT_NE(m.f.func->analysis_cache->provenance, nullptr);
  AnalysisManager::invalidate(*m.f.func, PreservedAnalysis::cfg_only());
  EXPECT_EQ(m.f.func->analysis_cache->provenance, nullptr);
}

// every case of AliasPinned.KnownNonnull, and what the sets know on top: a
// block argument that only gets an alloca and the non inbounds sum that stays
// inside an object
TEST(Provenance, NonNullAgreesWithIsKnownNonnull) {
  AliasModule m;
  using foptim::optim::is_known_nonnull;
  auto sel_ok = m.b.build_select(m.ptr, m.cond, m.alloca1, m.d);
  auto d_inb = m.b.build_ptr_add(m.d, m.k(8), true);
  auto d_plain = m.add(m.d, 8);
  auto sel_bad = m.b.build_select(m.ptr, m.cond, m.alloca1, m.a);
  Provenance p{*m.f.func};
  for (auto v : {m.alloca1, m.g1, m.d, sel_ok, d_inb, d_plain, m.a, m.c, m.null,
                 m.loaded_a, m.call_res, sel_bad}) {
    EXPECT_EQ(p.non_null(v), is_known_nonnull(v));
  }
  EXPECT_FALSE(is_known_nonnull(m.phi));
  EXPECT_TRUE(p.non_null(m.phi));
}
}  // namespace
