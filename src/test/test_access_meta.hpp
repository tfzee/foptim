#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/value.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/alias_verify.hpp"
#include "optim/analysis/mem/provenance.hpp"
#include "optim/helper/inline.hpp"

namespace {
using foptim::fir::AccessMeta;
using foptim::fir::MetaCopy;
using foptim::fir::ValueR;
using foptim::optim::AliasQuery;
using foptim::optim::AliasResult;
using foptim::optim::AliasSetTable;
using foptim::optim::Provenance;
using foptim::optim::SetId;

// f(p, q), the accesses are built per test
struct MetaModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR i32{ctx.data->get_int_type(32)};
  foptim::fir::TypeR ptr{ctx.data->get_ptr_type()};
  foptim::fir::FunctionR f;
  foptim::fir::Builder b;
  ValueR p, q;
  // a type tree: int and float are below char
  SetId chr, i, fl;

  explicit MetaModule(bool noalias = false)
      : f(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_void_type(), {ptr, ptr}))),
        b(f.builder()) {
    auto entry = f.func->get_entry();
    b.at_end(entry);
    p = ValueR{entry->args[0]};
    q = ValueR{entry->args[1]};
    entry->args[0]->noalias = noalias;
    entry->args[1]->noalias = noalias;
    auto &tab = ctx.data->alias_sets();
    chr = tab.create(std::span<const SetId>{});
    i = tab.create_object(chr);
    fl = tab.create_object(chr);
  }

  ValueR c(foptim::i64 v) {
    return ValueR{ctx.data->get_constant_value(v, i32)};
  }
  SetId scope() {
    return ctx.data->alias_sets().create(std::span<const SetId>{});
  }
  foptim::fir::Instr load(ValueR ptr_, AccessMeta m = {}) {
    auto l = b.build_load(i32, ptr_, false, false).as_instr();
    f.func->set_access_meta(l, std::move(m));
    return l;
  }
  foptim::fir::Instr store(ValueR ptr_, AccessMeta m = {}) {
    auto s = b.build_store(ptr_, c(1), false, false).as_instr();
    f.func->set_access_meta(s, std::move(m));
    return s;
  }
  static AccessMeta types(std::initializer_list<foptim::u32> t) {
    AccessMeta m;
    m.types.assign(t.begin(), t.end());
    return m;
  }
};
}  // namespace

TEST(AccessMetaQuery, TypesProveSameAddressApart) {
  MetaModule m;
  auto s = m.store(m.p, MetaModule::types({m.i}));
  auto l_float = m.load(m.p, MetaModule::types({m.fl}));
  auto l_int = m.load(m.p, MetaModule::types({m.i}));
  auto l_char = m.load(m.p, MetaModule::types({m.chr}));
  auto l_none = m.load(m.p);
  m.b.build_return();
  Provenance prov{*m.f.func};
  AliasQuery q{prov};
  EXPECT_EQ(q.alias(s, l_float), AliasResult::No);
  EXPECT_EQ(q.alias(l_float, s), AliasResult::No);
  EXPECT_EQ(q.alias(s, l_int), AliasResult::Must);
  EXPECT_EQ(q.alias(s, l_char), AliasResult::Must);
  EXPECT_EQ(q.alias(s, l_none), AliasResult::Must);
  EXPECT_TRUE(foptim::optim::verify_access_meta(*m.f.func));
}

TEST(AccessMetaQuery, FieldsOfOneStructAreApart) {
  MetaModule m;
  auto &tab = m.ctx.data->alias_sets();
  const SetId group = tab.create(std::span<const SetId>{});
  const SetId f0 = tab.create_object(group);
  const SetId f4 = tab.create_object(group);
  auto a = m.store(m.p, MetaModule::types({f0, m.i}));
  auto b = m.load(m.p, MetaModule::types({f4, m.i}));
  auto c = m.load(m.p, MetaModule::types({m.i}));
  m.b.build_return();
  Provenance prov{*m.f.func};
  AliasQuery q{prov};
  EXPECT_EQ(q.alias(a, b), AliasResult::No);
  EXPECT_NE(q.alias(a, c), AliasResult::No);
}

TEST(AccessMetaQuery, ScopesAreOneInstanceOnly) {
  MetaModule m;
  const SetId s1 = m.scope();
  const SetId s2 = m.scope();
  AccessMeta ma;
  ma.scope_in = {s1};
  ma.not_in = {s2};
  AccessMeta mb;
  mb.scope_in = {s2};
  mb.not_in = {s1};
  AccessMeta mc;
  mc.scope_in = {s2};
  auto a = m.store(m.p, ma);
  auto b = m.load(m.p, mb);
  // c is in the scope a excludes
  auto c = m.load(m.p, mc);
  m.b.build_return();
  Provenance prov{*m.f.func};
  AliasQuery q{prov};
  EXPECT_EQ(q.alias(a, b), AliasResult::No);
  EXPECT_EQ(q.alias(b, a), AliasResult::No);
  EXPECT_EQ(q.alias(a, c), AliasResult::No);
  EXPECT_EQ(q.alias(c, a), AliasResult::No);
  EXPECT_EQ(q.alias(b, c), AliasResult::Must);
  EXPECT_EQ(q.alias(a, b, true), AliasResult::Must);
  EXPECT_TRUE(foptim::optim::verify_access_meta(*m.f.func));
}

TEST(AccessMetaQuery, ScopeUnionNeedsEveryPartCovered) {
  MetaModule m;
  const SetId s1 = m.scope();
  const SetId s2 = m.scope();
  const SetId s3 = m.scope();
  auto &tab = m.ctx.data->alias_sets();
  const SetId either = tab.union_of(s1, s2);
  AccessMeta in_either;
  in_either.scope_in = {either};
  AccessMeta no_s1;
  no_s1.not_in = {s1};
  AccessMeta no_s1_s2;
  no_s1_s2.not_in = {s1, s2, s3};
  auto x = m.store(m.p, in_either);
  auto y = m.load(m.p, no_s1);
  auto z = m.load(m.p, no_s1_s2);
  m.b.build_return();
  Provenance prov{*m.f.func};
  AliasQuery q{prov};
  EXPECT_EQ(q.alias(x, y), AliasResult::Must);
  EXPECT_EQ(q.alias(x, z), AliasResult::No);
}

TEST(AccessMetaQuery, ModRefUsesMetadata) {
  MetaModule m;
  auto s = m.store(m.p, MetaModule::types({m.i}));
  auto l = m.load(m.p, MetaModule::types({m.fl}));
  m.b.build_return();
  Provenance prov{*m.f.func};
  AliasQuery q{prov};
  const auto loc = foptim::optim::MemLoc{
      .ptr = foptim::optim::decompose_ptr(m.p), .size = 4};
  const auto fl_meta = MetaModule::types({m.fl});
  EXPECT_EQ(q.mod_ref(s, loc, &fl_meta), foptim::optim::ModRef::NoModRef);
  EXPECT_EQ(q.mod_ref(s, loc), foptim::optim::ModRef::Mod);
  EXPECT_EQ(q.mod_ref(l, loc), foptim::optim::ModRef::Ref);
}

TEST(AccessMetaCopy, PoliciesKeepWhatTheyShould) {
  MetaModule m;
  const SetId s1 = m.scope();
  AccessMeta meta;
  meta.types = {m.i};
  meta.scope_in = {s1};
  meta.not_in = {m.scope()};
  auto l = m.load(m.p, meta);
  auto all = m.b.insert_copy(l);
  auto types = m.b.insert_copy(l, MetaCopy::Types);
  auto none = m.b.insert_copy(l, MetaCopy::None);
  m.b.build_return();
  const auto *all_m = m.f.func->get_access_meta(all);
  ASSERT_NE(all_m, nullptr);
  EXPECT_TRUE(*all_m == meta);
  const auto *types_m = m.f.func->get_access_meta(types);
  ASSERT_NE(types_m, nullptr);
  EXPECT_EQ(types_m->types, meta.types);
  EXPECT_TRUE(types_m->scope_in.empty());
  EXPECT_TRUE(types_m->not_in.empty());
  EXPECT_EQ(m.f.func->get_access_meta(none), nullptr);
  EXPECT_FALSE(none->has_access_meta);
  EXPECT_TRUE(foptim::optim::verify_access_meta(*m.f.func));
}

TEST(AccessMetaCopy, BlockCopyToAnotherFunction) {
  MetaModule m;
  AccessMeta meta;
  meta.types = {m.i};
  meta.scope_in = {m.scope()};
  auto st = m.store(m.p, meta);
  auto ld = m.load(m.q);
  m.b.build_return();

  auto other = m.ctx.data->create_function(
      "g",
      m.ctx.data->get_func_ty(m.ctx.data->get_void_type(), {m.ptr, m.ptr}));
  foptim::fir::ContextData::V2VMap subs;
  auto bb = m.ctx.data->copy(m.f.func->get_entry(), subs, false,
                             MetaCopy::Types, other.func);
  other.func->append_bbr(bb);
  ASSERT_EQ(bb->instructions.size(), 3U);
  const auto *copied = other.func->get_access_meta(bb->instructions[0]);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(copied->types, meta.types);
  EXPECT_TRUE(copied->scope_in.empty());
  EXPECT_EQ(other.func->get_access_meta(bb->instructions[1]), nullptr);
  EXPECT_NE(m.f.func->get_access_meta(st), nullptr);
  EXPECT_EQ(m.f.func->get_access_meta(ld), nullptr);
}

TEST(AccessMetaVerify, CatchesBrokenMetadata) {
  {
    MetaModule m;
    AccessMeta bad;
    bad.types = {9999};
    m.load(m.p, bad);
    m.b.build_return();
    EXPECT_FALSE(foptim::optim::verify_access_meta(*m.f.func));
  }
  {
    MetaModule m;
    m.load(m.p, MetaModule::types({m.i, m.fl}));
    m.b.build_return();
    EXPECT_FALSE(foptim::optim::verify_access_meta(*m.f.func));
  }
  {
    MetaModule m;
    const SetId s = m.scope();
    AccessMeta bad;
    bad.scope_in = {s};
    bad.not_in = {s};
    m.load(m.p, bad);
    m.b.build_return();
    EXPECT_FALSE(foptim::optim::verify_access_meta(*m.f.func));
  }
  {
    MetaModule m;
    auto sum = m.b.build_int_add(m.c(1), m.c(2));
    (void)sum;
    m.b.build_return();
    m.f.func->get_entry()->instructions[0]->has_access_meta = true;
    EXPECT_FALSE(foptim::optim::verify_access_meta(*m.f.func));
    m.f.func->get_entry()->instructions[0]->has_access_meta = false;
  }
  MetaModule ok;
  ok.load(ok.p, MetaModule::types({ok.i, ok.chr}));
  ok.b.build_return();
  EXPECT_TRUE(foptim::optim::verify_access_meta(*ok.f.func));
  EXPECT_TRUE(foptim::optim::verify_module_alias_sets(*ok.ctx.data));
}

// h(noalias a, noalias b, c): *a = 1; *b = 2; *c = 3
// f(x, y, z): h(x, y, z); h(y, x, z)
struct InlineScopeModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR i32{ctx.data->get_int_type(32)};
  foptim::fir::TypeR ptr{ctx.data->get_ptr_type()};
  foptim::fir::FunctionR h;
  foptim::fir::FunctionR f;
  std::array<foptim::fir::Instr, 2> calls{foptim::fir::Instr{},
                                          foptim::fir::Instr{}};

  InlineScopeModule()
      : h(ctx.data->create_function(
            "h",
            ctx.data->get_func_ty(ctx.data->get_void_type(), {ptr, ptr, ptr}))),
        f(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_void_type(),
                                       {ptr, ptr, ptr}))) {
    auto hb = h.builder();
    auto he = h.func->get_entry();
    he->args[0]->noalias = 1;
    he->args[1]->noalias = 1;
    hb.at_end(he);
    for (size_t k = 0; k < 3; k++) {
      auto s = hb.build_store(ValueR{he->args[k]}, c(k + 1), false, false);
      // a scope of the callee itself
      if (k == 2) {
        AccessMeta m;
        m.scope_in = {ctx.data->alias_sets().create(std::span<const SetId>{})};
        h.func->set_access_meta(s.as_instr(), m);
      }
    }
    hb.build_return();

    auto fb = f.builder();
    auto fe = f.func->get_entry();
    fb.at_end(fe);
    auto fv = ValueR{ctx.data->get_constant_value(h)};
    std::array<foptim::fir::TypeR, 1> void_ty{ctx.data->get_void_type()};
    std::array<ValueR, 3> a1{ValueR{fe->args[0]}, ValueR{fe->args[1]},
                             ValueR{fe->args[2]}};
    std::array<ValueR, 3> a2{ValueR{fe->args[1]}, ValueR{fe->args[0]},
                             ValueR{fe->args[2]}};
    calls[0] = fb.build_call(fv, h.func->func_ty, void_ty, a1);
    calls[1] = fb.build_call(fv, h.func->func_ty, void_ty, a2);
    fb.build_return();
  }
  ValueR c(foptim::i64 v) {
    return ValueR{ctx.data->get_constant_value(v, i32)};
  }

  std::vector<foptim::fir::Instr> stores() {
    std::vector<foptim::fir::Instr> r;
    for (auto bb : f.func->basic_blocks) {
      for (auto i : bb->instructions) {
        if (i->is(foptim::fir::InstrType::StoreInstr)) {
          r.push_back(i);
        }
      }
    }
    return r;
  }
};

TEST(AccessMetaInline, NoaliasParametersBecomeScopes) {
  InlineScopeModule m;
  ASSERT_TRUE(foptim::optim::inline_call(m.calls[0]));
  auto st = m.stores();
  ASSERT_EQ(st.size(), 3U);
  const auto *a = m.f.func->get_access_meta(st[0]);
  const auto *b = m.f.func->get_access_meta(st[1]);
  const auto *c = m.f.func->get_access_meta(st[2]);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(a->scope_in.size(), 1U);
  ASSERT_EQ(b->scope_in.size(), 1U);
  EXPECT_NE(a->scope_in[0], b->scope_in[0]);
  EXPECT_NE(std::ranges::find(a->not_in, b->scope_in[0]), a->not_in.end());
  EXPECT_NE(std::ranges::find(b->not_in, a->scope_in[0]), b->not_in.end());
  EXPECT_TRUE(foptim::optim::verify_access_meta(*m.f.func));

  Provenance prov{*m.f.func};
  AliasQuery q{prov};
  EXPECT_EQ(q.alias(st[0], st[1]), AliasResult::No);
}

TEST(AccessMetaInline, EveryInliningGetsFreshScopes) {
  InlineScopeModule m;
  ASSERT_TRUE(foptim::optim::inline_call(m.calls[0]));
  ASSERT_TRUE(foptim::optim::inline_call(m.calls[1]));
  auto st = m.stores();
  ASSERT_EQ(st.size(), 6U);
  const auto *first = m.f.func->get_access_meta(st[0]);
  const auto *second = m.f.func->get_access_meta(st[3]);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first->scope_in[0], second->scope_in[0]);
  const auto *c1 = m.f.func->get_access_meta(st[2]);
  const auto *c2 = m.f.func->get_access_meta(st[5]);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  ASSERT_FALSE(c1->scope_in.empty());
  ASSERT_FALSE(c2->scope_in.empty());
  EXPECT_NE(c1->scope_in[0], c2->scope_in[0]);
  EXPECT_TRUE(foptim::optim::verify_access_meta(*m.f.func));
  EXPECT_TRUE(foptim::optim::verify_module_alias_sets(*m.ctx.data));

  // both store to x, the instances must not be proven apart
  Provenance prov{*m.f.func};
  AliasQuery q{prov};
  EXPECT_NE(q.alias(st[0], st[4]), AliasResult::No);
}
