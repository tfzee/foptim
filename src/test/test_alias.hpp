#pragma once
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/value.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/basic_alias_test.hpp"
#include "optim/helper/helper.hpp"

namespace {
using foptim::optim::AliasAnalyis;
using AAResult = AliasAnalyis::AAResult;
using foptim::fir::ValueR;

// f(a, b, c noalias, d nonnull, n, cond), all pointers are 8 byte accesses
// unless a case says otherwise
struct AliasModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR i64{ctx.data->get_int_type(64)};
  foptim::fir::TypeR i1{ctx.data->get_int_type(1)};
  foptim::fir::TypeR ptr{ctx.data->get_ptr_type()};
  foptim::fir::FunctionR f;
  foptim::fir::Builder b;
  foptim::fir::BasicBlock join{foptim::fir::BasicBlock::invalid()};

  ValueR a, bb, c, d, n, cond;
  ValueR alloca1, alloca2, g1, g2, null;
  ValueR loaded_a, loaded_b, phi;
  ValueR call_res, int2ptr;

  ValueR k(foptim::i64 v) {
    return ValueR{ctx.data->get_constant_value(v, i64)};
  }
  ValueR add(ValueR p, foptim::i64 off) { return b.build_ptr_add(p, k(off)); }

  AliasModule()
      : f(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_void_type(),
                                       {ptr, ptr, ptr, ptr, i64, i1}))),
        b(f.builder()) {
    auto entry = f.func->get_entry();
    join = b.append_bb();
    join.add_arg(ctx.data->storage.insert_bb_arg(join, ptr));
    b.at_end(entry);
    a = ValueR{entry->args[0]};
    bb = ValueR{entry->args[1]};
    c = ValueR{entry->args[2]};
    d = ValueR{entry->args[3]};
    n = ValueR{entry->args[4]};
    cond = ValueR{entry->args[5]};
    entry->args[2]->noalias = 1;
    entry->args[3]->nonnull = 1;

    alloca1 = b.build_alloca(k(32));
    alloca2 = b.build_alloca(k(32));
    g1 =
        ValueR{ctx.data->get_constant_value(ctx.data->insert_global("g1", 32))};
    g2 =
        ValueR{ctx.data->get_constant_value(ctx.data->insert_global("g2", 32))};
    null = ValueR{ctx.data->get_constant_null()};
    loaded_a = b.build_load(ptr, a, false, false);
    loaded_b = b.build_load(ptr, bb, false, false);
    int2ptr =
        b.build_conversion_op(n, ptr, foptim::fir::ConversionSubType::IntToPtr);
    std::array<ValueR, 0> no_args{};
    auto ext = ctx.data->create_function("ext", ctx.data->get_func_ty(ptr, {}));
    {
      auto eb = ext.builder();
      eb.at_end(ext.func->get_entry());
      eb.build_return(null);
    }
    call_res = b.build_call(ValueR{ctx.data->get_constant_value(ext)},
                            ext.func->func_ty, ptr, no_args);
    auto br = b.build_branch(join);
    br.add_bb_arg(0, alloca1);
    phi = ValueR{join->args[0]};
    b.at_end(join);
    b.build_return();
  }
};

struct AliasCase {
  std::string name;
  ValueR x;
  foptim::u32 x_size;
  ValueR y;
  foptim::u32 y_size;
  AAResult expected;
};

std::vector<AliasCase> alias_cases(AliasModule &m) {
  const auto N = AAResult::NoAlias;
  const auto M = AAResult::MightAlias;
  const auto A = AAResult::Alias;
  auto ok = [&](const char *name, ValueR x, foptim::u32 xs, ValueR y,
                foptim::u32 ys,
                AAResult r) { return AliasCase{name, x, xs, y, ys, r}; };
  std::vector<AliasCase> cs;
  // same pointer
  cs.push_back(ok("same_arg", m.a, 8, m.a, 8, A));
  cs.push_back(ok("same_alloca", m.alloca1, 8, m.alloca1, 4, A));
  // constant offsets from one base, sizes decide
  // the argument itself has no known offset, only the PtrAdd of it has one
  cs.push_back(ok("arg_vs_arg_plus_offset", m.a, 8, m.add(m.a, 8), 8, N));
  cs.push_back(ok("arg_adjacent", m.add(m.a, 8), 8, m.add(m.a, 16), 8, N));
  cs.push_back(ok("arg_partial_overlap", m.a, 8, m.add(m.a, 4), 8, M));
  cs.push_back(ok("arg_unknown_size", m.a, 0, m.add(m.a, 8), 8, M));
  cs.push_back(
      ok("arg_nested_ptradd", m.add(m.add(m.a, 8), 8), 8, m.add(m.a, 8), 8, N));
  cs.push_back(ok("arg_same_offset_two_instrs", m.add(m.a, 16), 8,
                  m.add(m.a, 16), 8, A));
  cs.push_back(ok("arg_variable_index", m.add(m.a, 0), 8,
                  m.b.build_ptr_add(m.a, m.n), 8, M));
  cs.push_back(ok("alloca_adjacent", m.alloca1, 8, m.add(m.alloca1, 8), 8, N));
  cs.push_back(ok("alloca_overlap", m.alloca1, 16, m.add(m.alloca1, 8), 8, M));
  cs.push_back(ok("global_adjacent", m.g1, 8, m.add(m.g1, 8), 8, N));
  cs.push_back(ok("global_overlap", m.g1, 16, m.add(m.g1, 8), 8, M));
  // different roots
  cs.push_back(ok("alloca_alloca", m.alloca1, 8, m.alloca2, 8, N));
  cs.push_back(ok("alloca_alloca_offsets", m.add(m.alloca1, 8), 8,
                  m.add(m.alloca2, 8), 8, N));
  cs.push_back(ok("alloca_arg", m.alloca1, 8, m.a, 8, N));
  cs.push_back(ok("alloca_global", m.alloca1, 8, m.g1, 8, N));
  cs.push_back(ok("global_global", m.g1, 8, m.g2, 8, N));
  // globals may be reached through argument pointers
  cs.push_back(ok("global_arg", m.g1, 8, m.a, 8, M));
  cs.push_back(ok("arg_arg", m.a, 8, m.bb, 8, M));
  cs.push_back(ok("arg_arg_offsets", m.a, 8, m.add(m.bb, 8), 8, M));
  // null
  cs.push_back(ok("null_arg", m.null, 8, m.a, 8, N));
  cs.push_back(ok("null_alloca", m.null, 8, m.alloca1, 8, N));
  cs.push_back(ok("null_null", m.null, 8, m.null, 8, A));
  // noalias argument
  cs.push_back(ok("noalias_arg", m.c, 8, m.a, 8, N));
  cs.push_back(ok("noalias_global", m.c, 8, m.g1, 8, N));
  cs.push_back(ok("noalias_alloca", m.c, 8, m.alloca1, 8, N));
  cs.push_back(ok("noalias_loaded", m.c, 8, m.loaded_a, 8, M));
  cs.push_back(ok("noalias_noalias_self", m.c, 8, m.add(m.c, 8), 8, N));
  // unknown provenance
  cs.push_back(ok("loaded_alloca", m.loaded_a, 8, m.alloca1, 8, M));
  cs.push_back(ok("loaded_arg", m.loaded_a, 8, m.a, 8, M));
  cs.push_back(ok("loaded_loaded", m.loaded_a, 8, m.loaded_b, 8, M));
  cs.push_back(ok("call_alloca", m.call_res, 8, m.alloca1, 8, M));
  cs.push_back(ok("inttoptr_alloca", m.int2ptr, 8, m.alloca1, 8, M));
  cs.push_back(ok("phi_alloca2", m.phi, 8, m.alloca2, 8, N));
  cs.push_back(ok("phi_arg", m.phi, 8, m.a, 8, N));
  // select joins
  auto sel_local = m.b.build_select(m.ptr, m.cond, m.alloca1, m.alloca2);
  auto sel_mixed = m.b.build_select(m.ptr, m.cond, m.alloca1, m.a);
  cs.push_back(ok("select_local_alloca", sel_local, 8, m.alloca1, 8, M));
  cs.push_back(ok("select_local_arg", sel_local, 8, m.a, 8, N));
  cs.push_back(ok("select_mixed_alloca", sel_mixed, 8, m.alloca2, 8, N));
  cs.push_back(ok("select_mixed_arg", sel_mixed, 8, m.a, 8, M));
  // an unknown base plus a constant offset is taken as "offset c of one
  // object", but different arguments and loaded pointers can point into the
  // same object
  cs.push_back(ok("loaded_vs_loaded_plus_offset", m.loaded_a, 8,
                  m.add(m.loaded_b, 8), 8, M));
  cs.push_back(ok("arg_plus_offset_vs_arg_plus_offset", m.add(m.a, 8), 8,
                  m.add(m.bb, 16), 8, M));
  cs.push_back(ok("loaded_plus_offset_vs_loaded_plus_offset",
                  m.add(m.loaded_a, 0), 8, m.add(m.loaded_b, 8), 8, M));
  cs.push_back(ok("call_plus_offset_vs_loaded_plus_offset",
                  m.add(m.call_res, 0), 8, m.add(m.loaded_b, 8), 8, M));
  // the constant is truncated to 32 bit: [-4, 4) and [0, 8) overlap
  cs.push_back(
      ok("negative_offset_wraps", m.add(m.alloca1, -4), 8, m.alloca1, 8, M));
  return cs;
}
}  // namespace

TEST(AliasPinned, Answers) {
  AliasModule m;
  ASSERT_TRUE(m.ctx.data->verify());
  AliasAnalyis aa;
  for (const auto &c : alias_cases(m)) {
    EXPECT_EQ(aa.alias(c.x, c.y, c.x_size, c.y_size), c.expected)
        << c.name << " (x, y)";
    EXPECT_EQ(aa.alias(c.y, c.x, c.y_size, c.x_size), c.expected)
        << c.name << " (y, x)";
  }
}

TEST(AliasPinned, KnownLocalStack) {
  AliasModule m;
  AliasAnalyis aa;
  EXPECT_TRUE(aa.is_known_local_stack(m.alloca1));
  EXPECT_TRUE(aa.is_known_local_stack(m.add(m.alloca1, 8)));
  EXPECT_TRUE(aa.is_known_local_stack(
      m.b.build_select(m.ptr, m.cond, m.alloca1, m.alloca2)));
  EXPECT_FALSE(
      aa.is_known_local_stack(m.b.build_select(m.ptr, m.cond, m.alloca1, m.a)));
  EXPECT_FALSE(aa.is_known_local_stack(m.a));
  EXPECT_FALSE(aa.is_known_local_stack(m.c));
  EXPECT_FALSE(aa.is_known_local_stack(m.g1));
  EXPECT_FALSE(aa.is_known_local_stack(m.loaded_a));
  EXPECT_FALSE(aa.is_known_local_stack(m.null));
}

TEST(AliasPinned, KnownNonnull) {
  AliasModule m;
  using foptim::optim::is_known_nonnull;
  EXPECT_TRUE(is_known_nonnull(m.alloca1));
  EXPECT_TRUE(is_known_nonnull(m.g1));
  EXPECT_TRUE(is_known_nonnull(m.d));
  EXPECT_TRUE(
      is_known_nonnull(m.b.build_select(m.ptr, m.cond, m.alloca1, m.d)));
  EXPECT_TRUE(is_known_nonnull(m.b.build_ptr_add(m.d, m.k(8), true)));
  // without inbounds the addition may wrap to null
  EXPECT_FALSE(is_known_nonnull(m.add(m.d, 8)));
  EXPECT_FALSE(is_known_nonnull(m.a));
  EXPECT_FALSE(is_known_nonnull(m.c));
  EXPECT_FALSE(is_known_nonnull(m.null));
  EXPECT_FALSE(is_known_nonnull(m.loaded_a));
  EXPECT_FALSE(is_known_nonnull(m.call_res));
  EXPECT_FALSE(is_known_nonnull(m.phi));
  EXPECT_FALSE(
      is_known_nonnull(m.b.build_select(m.ptr, m.cond, m.alloca1, m.a)));
}

TEST(AliasQuery, ResultKinds) {
  AliasModule m;
  using foptim::optim::AliasQuery;
  using foptim::optim::AliasResult;
  foptim::optim::Provenance prov{*m.f.func};
  AliasQuery q{prov};
  EXPECT_EQ(q.alias(m.a, 8, m.a, 8), AliasResult::Must);
  EXPECT_EQ(q.alias(m.add(m.a, 8), 8, m.add(m.a, 8), 4), AliasResult::Must);
  EXPECT_EQ(q.alias(m.a, 16, m.add(m.a, 8), 8), AliasResult::Partial);
  EXPECT_EQ(q.alias(m.a, 8, m.add(m.a, 8), 8), AliasResult::No);
  EXPECT_EQ(q.alias(m.a, 0, m.add(m.a, 8), 8), AliasResult::May);
  EXPECT_EQ(q.alias(m.a, 8, m.bb, 8), AliasResult::May);
  EXPECT_EQ(q.alias(m.alloca1, 8, m.a, 8), AliasResult::No);
  EXPECT_EQ(q.alias(m.null, 8, m.a, 8), AliasResult::No);
}

TEST(AliasQuery, ModRef) {
  AliasModule m;
  using foptim::optim::AliasQuery;
  using foptim::optim::ModRef;
  foptim::optim::Provenance prov{*m.f.func};
  AliasQuery q{prov};
  foptim::optim::MemLoc stack{.ptr = foptim::optim::decompose_ptr(m.alloca2),
                              .size = 8};
  foptim::optim::MemLoc arg{.ptr = foptim::optim::decompose_ptr(m.a),
                            .size = 8};
  // the call gets no pointer and the alloca never escapes
  EXPECT_EQ(q.mod_ref(m.call_res.as_instr(), stack), ModRef::NoModRef);
  EXPECT_EQ(q.mod_ref(m.call_res.as_instr(), arg), ModRef::ModRef);
  // loads of other memory do not touch it
  EXPECT_EQ(q.mod_ref(m.loaded_a.as_instr(), stack), ModRef::NoModRef);
  EXPECT_EQ(q.mod_ref(m.loaded_a.as_instr(), arg), ModRef::Ref);
}
