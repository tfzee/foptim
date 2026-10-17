#include <gtest/gtest.h>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function_ref.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"

namespace {
using foptim::fir::ValueR;
using foptim::optim::SCEVExpr;

// entry: br loop(0)
// loop(i): i1 = i + 1; c = i1 <pred> limit; cond_br c, loop(i1), exit
// exit: ret i1
// `limit` is the second function argument (invariant) or the constant
struct CountingLoop {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::FunctionR func;
  foptim::fir::BasicBlock loop_bb{foptim::fir::BasicBlock::invalid()};
  ValueR i_arg;
  ValueR i_next;

  CountingLoop(foptim::fir::ICmpInstrSubType pred, foptim::i64 step,
               std::optional<foptim::i64> const_limit, foptim::i64 start = 0)
      : func(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_int_type(32),
                                       {ctx.data->get_int_type(32)}))) {
    auto i32 = ctx.data->get_int_type(32);
    auto builder = func.builder();
    auto entry_bb = func.func->get_entry();
    loop_bb = builder.append_bb();
    loop_bb.add_arg(ctx.data->storage.insert_bb_arg(loop_bb, i32));
    auto exit_bb = builder.append_bb();

    builder.at_end(entry_bb);
    auto br = builder.build_branch(loop_bb);
    br.add_bb_arg(0, ValueR{ctx.data->get_constant_value(start, i32)});

    builder.at_end(loop_bb);
    i_arg = ValueR{loop_bb->args[0]};
    i_next = builder.build_int_add(
        i_arg, ValueR{ctx.data->get_constant_value(step, i32)});
    ValueR limit = const_limit ? ValueR{ctx.data->get_constant_value(
                                     *const_limit, i32)}
                               : ValueR{entry_bb->args[0]};
    auto cond = builder.build_int_cmp(i_next, limit, pred);
    auto cbr = builder.build_cond_branch(cond, loop_bb, exit_bb);
    cbr.add_bb_arg(0, i_next);

    builder.at_end(exit_bb);
    builder.build_return(i_next);
  }
};

struct LoopSetup {
  foptim::optim::CFG cfg;
  foptim::optim::Dominators dom;
  foptim::optim::LoopInfoAnalysis loops;
  explicit LoopSetup(foptim::fir::Function &f)
      : cfg(f), dom(cfg), loops(dom) {}
};
} // namespace

TEST(ScalarEvoTest, InductionVariables) {
  using foptim::fir::ICmpInstrSubType;
  CountingLoop l{ICmpInstrSubType::SLT, 1, 10};
  LoopSetup s{*l.func.func};
  ASSERT_EQ(s.loops.info.size(), 1);
  foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};

  auto i = evo.get(l.i_arg);
  auto i1 = evo.get(l.i_next);
  ASSERT_TRUE(evo.is_const_addrec(i));
  ASSERT_TRUE(evo.is_const_addrec(i1));
  // i in iteration 7 is 7, i + 1 is 8
  EXPECT_EQ(evo.eval_at(i, 7), 7);
  EXPECT_EQ(evo.eval_at(i1, 7), 8);
  // the expression of iteration k = 4 is the constant 5
  auto at4 = evo.at_iteration(i1, evo.mk_const(4, 32));
  ASSERT_NE(at4, foptim::optim::InvalidSCEV);
  EXPECT_TRUE(evo.is_const(at4));
  EXPECT_EQ(evo.expr(at4).cval_i(), 5);
}

TEST(ScalarEvoTest, FoldingAndMax) {
  using foptim::fir::ICmpInstrSubType;
  CountingLoop l{ICmpInstrSubType::SLT, 1, 10};
  LoopSetup s{*l.func.func};
  foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};

  auto c3 = evo.mk_const(3, 32);
  auto c5 = evo.mk_const(5, 32);
  EXPECT_EQ(evo.expr(evo.mk_add(c3, c5)).cval_i(), 8);
  EXPECT_EQ(evo.expr(evo.mk_mul(c3, c5)).cval_i(), 15);
  EXPECT_EQ(evo.expr(evo.mk_max(c3, evo.mk_const(-7, 32), true)).cval_i(), 3);
  // -7 is a huge unsigned number
  EXPECT_EQ(evo.expr(evo.mk_max(c3, evo.mk_const(-7, 32), false)).cval_i(), -7);
  EXPECT_EQ(evo.expr(evo.mk_resize(evo.mk_const(-1, 32), 64, false)).cval_i(),
            0xFFFFFFFFLL);
  EXPECT_EQ(evo.expr(evo.mk_resize(evo.mk_const(0x1ffffffffLL, 64), 32, false))
                .cval_i(),
            -1);
  // a width mismatch is not an expression
  EXPECT_EQ(evo.expr(evo.mk_add(c3, evo.mk_const(1, 64))).t,
            SCEVExpr::Type::Unknown);
  // max(x, x) is x, a select on a constant condition is the chosen side
  auto n = evo.get(ValueR{l.func.func->get_entry()->args[0]});
  ASSERT_TRUE(evo.is_invariant(n));
  EXPECT_EQ(evo.mk_max(n, n, true), n);
  EXPECT_EQ(evo.mk_select(evo.mk_const(1, 1), c3, c5), c3);
  EXPECT_EQ(evo.mk_select(evo.mk_const(0, 1), c3, c5), c5);
}

TEST(TripCountTest, ConstantAffine) {
  using foptim::fir::ICmpInstrSubType;
  // i1 = i + 3 runs while i1 <= 10: 3, 6, 9 pass, 12 exits => exit_iter 3
  CountingLoop l{ICmpInstrSubType::SLE, 3, 10};
  LoopSetup s{*l.func.func};
  foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};
  foptim::optim::TripCountAnalysis tc;
  ASSERT_TRUE(tc.update(s.cfg, s.dom, s.loops.info[0], evo));
  ASSERT_TRUE(tc.const_exit_iter.has_value());
  EXPECT_EQ(*tc.const_exit_iter, 3);
  EXPECT_EQ(evo.expr(tc.exit_iter).cval_i(), 3);
}

TEST(TripCountTest, SymbolicLimit) {
  using foptim::fir::ICmpInstrSubType;
  // i1 = i + 1 while i1 < n: exit_iter = max(n, 1) - 1
  CountingLoop l{ICmpInstrSubType::SLT, 1, std::nullopt};
  LoopSetup s{*l.func.func};
  foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};
  foptim::optim::TripCountAnalysis tc;
  ASSERT_TRUE(tc.update(s.cfg, s.dom, s.loops.info[0], evo));
  EXPECT_FALSE(tc.const_exit_iter.has_value());
  EXPECT_TRUE(evo.is_invariant(tc.exit_iter));
  EXPECT_TRUE(evo.is_expandable(tc.exit_iter));

  // the exit value of i1 is i1 at the exit iteration: 1 + exit_iter
  auto end = evo.at_iteration(evo.get(l.i_next), tc.exit_iter);
  ASSERT_NE(end, foptim::optim::InvalidSCEV);
  EXPECT_TRUE(evo.is_expandable(end));
  EXPECT_FALSE(evo.is_const(end));
}

TEST(TripCountTest, SymbolicLimitNeedsSafeShape) {
  using foptim::fir::ICmpInstrSubType;
  // step 2 with a symbolic limit is not supported (yet)
  {
    CountingLoop l{ICmpInstrSubType::SLT, 2, std::nullopt};
    LoopSetup s{*l.func.func};
    foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};
    foptim::optim::TripCountAnalysis tc;
    EXPECT_FALSE(tc.update(s.cfg, s.dom, s.loops.info[0], evo));
  }
  // signed compare with a start that is not known to be non negative could
  // overflow the difference: i starts at -5
  {
    CountingLoop l{ICmpInstrSubType::SLT, 1, std::nullopt, -5};
    LoopSetup s{*l.func.func};
    foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};
    foptim::optim::TripCountAnalysis tc;
    EXPECT_FALSE(tc.update(s.cfg, s.dom, s.loops.info[0], evo));
  }
  // the same loop with an unsigned compare is fine
  {
    CountingLoop l{ICmpInstrSubType::ULT, 1, std::nullopt, -5};
    LoopSetup s{*l.func.func};
    foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};
    foptim::optim::TripCountAnalysis tc;
    EXPECT_TRUE(tc.update(s.cfg, s.dom, s.loops.info[0], evo));
  }
}
