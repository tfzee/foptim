#include <gtest/gtest.h>

#include <array>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "optim/helper/arg_gain.hpp"

namespace {
using foptim::fir::ICmpInstrSubType;
using foptim::fir::ValueR;
using foptim::optim::ArgFact;
using foptim::optim::estimate_arg_gain;

struct GainModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR i32{ctx.data->get_int_type(32)};
  foptim::fir::TypeR ptr{ctx.data->get_ptr_type()};
  foptim::fir::FunctionR f;

  explicit GainModule(bool ptr_arg = false)
      : f(ctx.data->create_function(
            "f", ptr_arg ? ctx.data->get_func_ty(i32, {i32, ptr})
                         : ctx.data->get_func_ty(i32, {i32, i32}))) {}

  ValueR c(foptim::i64 v) {
    return ValueR{ctx.data->get_constant_value(v, i32)};
  }
  ValueR arg(size_t i) { return ValueR{f.func->get_entry()->args[i]}; }
};

constexpr foptim::u32 Big = 1000;
}  // namespace

// f(a, b): if (b == 0) return a; return (a + 1) * 3
TEST(ArgGain, ConstantBranchKillsOtherSide) {
  GainModule m;
  auto b = m.f.builder();
  auto entry = m.f.func->get_entry();
  auto fast = b.append_bb();
  auto slow = b.append_bb();
  b.at_end(entry);
  b.build_cond_branch(
      b.build_int_cmp(m.arg(0 + 1), m.c(0), ICmpInstrSubType::EQ), fast, slow);
  b.at_end(fast);
  b.build_return(m.arg(0));
  b.at_end(slow);
  b.build_return(b.build_int_mul(b.build_int_add(m.arg(0), m.c(1)), m.c(3)));
  ASSERT_TRUE(m.ctx.data->verify());

  std::array<ArgFact, 2> zero{ArgFact::unknown(), ArgFact::int_(32, 0)};
  auto g = estimate_arg_gain(*m.f.func, zero, Big);
  EXPECT_FALSE(g.exceeded);
  EXPECT_EQ(g.branches_folded, 1);
  EXPECT_EQ(g.folded, 1);
  EXPECT_EQ(g.dead, 3);
  EXPECT_EQ(g.remaining, 1);

  std::array<ArgFact, 2> one{ArgFact::unknown(), ArgFact::int_(32, 1)};
  g = estimate_arg_gain(*m.f.func, one, Big);
  EXPECT_EQ(g.dead, 1);
  EXPECT_EQ(g.remaining, 3);

  std::array<ArgFact, 2> none{};
  g = estimate_arg_gain(*m.f.func, none, Big);
  EXPECT_EQ(g.folded, 0);
  EXPECT_EQ(g.dead, 0);
  EXPECT_EQ(g.remaining, m.f.func->n_instrs());

  g = estimate_arg_gain(*m.f.func, none, 2);
  EXPECT_TRUE(g.exceeded);
}

// f(a, b): x = (b + 2) * 4; return a + x, everything but the last add folds
TEST(ArgGain, ArithmeticFolds) {
  GainModule m;
  auto b = m.f.builder();
  b.at_end(m.f.func->get_entry());
  auto x = b.build_int_mul(b.build_int_add(m.arg(1), m.c(2)), m.c(4));
  b.build_return(b.build_int_add(m.arg(0), x));
  std::array<ArgFact, 2> facts{ArgFact::unknown(), ArgFact::int_(32, 5)};
  auto g = estimate_arg_gain(*m.f.func, facts, Big);
  EXPECT_EQ(g.folded, 2);
  EXPECT_EQ(g.remaining, 2);
}

// f(a, p): if (p == null) return 0; return a
TEST(ArgGain, NullCheck) {
  GainModule m(true);
  auto b = m.f.builder();
  auto entry = m.f.func->get_entry();
  auto is_null = b.append_bb();
  auto other = b.append_bb();
  b.at_end(entry);
  auto null = ValueR{m.ctx.data->get_constant_null()};
  b.build_cond_branch(b.build_int_cmp(m.arg(1), null, ICmpInstrSubType::EQ),
                      is_null, other);
  b.at_end(is_null);
  b.build_return(m.c(0));
  b.at_end(other);
  b.build_return(m.arg(0));
  ASSERT_TRUE(m.ctx.data->verify());

  std::array<ArgFact, 2> nn{ArgFact::unknown(), ArgFact::nonnull()};
  auto g = estimate_arg_gain(*m.f.func, nn, Big);
  EXPECT_EQ(g.branches_folded, 1);
  EXPECT_EQ(g.dead, 1);
  EXPECT_EQ(g.remaining, 1);

  std::array<ArgFact, 2> isnull{ArgFact::unknown(), ArgFact::null()};
  g = estimate_arg_gain(*m.f.func, isnull, Big);
  EXPECT_EQ(g.branches_folded, 1);
  EXPECT_EQ(g.dead, 1);
}

// both edges into the merge block pass 5, so the test on it folds
TEST(ArgGain, JoinOfEqualConstants) {
  GainModule m;
  auto b = m.f.builder();
  auto entry = m.f.func->get_entry();
  auto l = b.append_bb();
  auto r = b.append_bb();
  auto merge = b.append_bb();
  auto yes = b.append_bb();
  auto no = b.append_bb();
  merge.add_arg(m.ctx.data->storage.insert_bb_arg(merge, m.i32));
  b.at_end(entry);
  b.build_cond_branch(b.build_int_cmp(m.arg(1), m.c(0), ICmpInstrSubType::EQ),
                      l, r);
  b.at_end(l);
  b.build_branch(merge).add_bb_arg(0, m.c(5));
  b.at_end(r);
  b.build_branch(merge).add_bb_arg(0, m.c(5));
  b.at_end(merge);
  b.build_cond_branch(
      b.build_int_cmp(ValueR{merge->args[0]}, m.c(5), ICmpInstrSubType::EQ),
      yes, no);
  b.at_end(yes);
  b.build_return(m.arg(0));
  b.at_end(no);
  b.build_return(m.c(0));
  ASSERT_TRUE(m.ctx.data->verify());

  std::array<ArgFact, 2> none{};
  auto g = estimate_arg_gain(*m.f.func, none, Big);
  EXPECT_EQ(g.branches_folded, 1);
  EXPECT_EQ(g.dead, 1);
  EXPECT_EQ(g.folded, 1);

  // different constants on the two edges join to unknown
  auto term = r->instructions.back();
  term.clear_bb_args(0);
  term.add_bb_arg(0, m.c(6));
  g = estimate_arg_gain(*m.f.func, none, Big);
  EXPECT_EQ(g.branches_folded, 0);
}
