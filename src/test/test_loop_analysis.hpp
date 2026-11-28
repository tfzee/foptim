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
    ValueR limit = const_limit
                       ? ValueR{ctx.data->get_constant_value(*const_limit, i32)}
                       : ValueR{entry_bb->args[0]};
    auto cond = builder.build_int_cmp(i_next, limit, pred);
    auto cbr = builder.build_cond_branch(cond, loop_bb, exit_bb);
    cbr.add_bb_arg(0, i_next);

    builder.at_end(exit_bb);
    builder.build_return(i_next);
  }
};

// entry: br loop(0, d_start)
// loop(i, d): i1 = i + 1; d1 = ext(i1) * 8; c = i1 < 10; cond_br c, loop(i1,
// d1), exit exit: ret d is the shape LoopRotate leaves for a header value base
// + 8 * i that the body uses. i is i64, or i32 sign extended to i64 when narrow
struct DerivedLoop {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::FunctionR func;
  ValueR i_arg;
  ValueR i_next;
  ValueR d_arg;

  DerivedLoop(foptim::i64 d_start, bool narrow)
      : func(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_void_type(), {}))) {
    auto i64 = ctx.data->get_int_type(64);
    auto ity = narrow ? ctx.data->get_int_type(32) : i64;
    auto builder = func.builder();
    auto entry_bb = func.func->get_entry();
    auto loop_bb = builder.append_bb();
    loop_bb.add_arg(ctx.data->storage.insert_bb_arg(loop_bb, ity));
    loop_bb.add_arg(ctx.data->storage.insert_bb_arg(loop_bb, i64));
    auto exit_bb = builder.append_bb();

    builder.at_end(entry_bb);
    auto br = builder.build_branch(loop_bb);
    br.add_bb_arg(0, ValueR{ctx.data->get_constant_value(0, ity)});
    br.add_bb_arg(0, ValueR{ctx.data->get_constant_value(d_start, i64)});

    builder.at_end(loop_bb);
    i_arg = ValueR{loop_bb->args[0]};
    d_arg = ValueR{loop_bb->args[1]};
    i_next = builder.build_int_add(
        i_arg, ValueR{ctx.data->get_constant_value(1, ity)});
    auto wide = narrow ? builder.build_sext(i_next, i64) : i_next;
    auto d_next = builder.build_int_mul(
        wide, ValueR{ctx.data->get_constant_value(8, i64)});
    auto cond = builder.build_int_cmp(
        i_next, ValueR{ctx.data->get_constant_value(10, ity)},
        foptim::fir::ICmpInstrSubType::SLT);
    auto cbr = builder.build_cond_branch(cond, loop_bb, exit_bb);
    cbr.add_bb_arg(0, i_next);
    cbr.add_bb_arg(0, d_next);

    builder.at_end(exit_bb);
    builder.build_return();
  }
};

struct LoopSetup {
  foptim::optim::CFG cfg;
  foptim::optim::Dominators dom;
  foptim::optim::LoopInfoAnalysis loops;
  explicit LoopSetup(foptim::fir::Function &f) : cfg(f), dom(cfg), loops(dom) {}
};

// three deep nest with a sibling of the middle loop, same exit condition
// entry: c = arg != 0; br h0
// h0:  br h1                       L0 = {h0, h1, h2, x1, x0, sib, x2}
// h1:  br h2                       L1 = {h1, h2, x1}
// h2:  cond_br c, h2, x1           L2 = {h2}
// x1:  cond_br c, h1, x0
// x0:  br sib
// sib: cond_br c, sib, x2          L3 = {sib}, a sibling of L1 inside L0
// x2:  cond_br c, h0, exit
// exit: ret 0
struct ThreeDeepNest {
  using BB = foptim::fir::BasicBlock;
  foptim::fir::Context ctx{nullptr};
  foptim::fir::FunctionR func;
  BB h0{BB::invalid()}, h1{BB::invalid()}, h2{BB::invalid()}, x1{BB::invalid()},
      x0{BB::invalid()}, sib{BB::invalid()}, x2{BB::invalid()};

  ThreeDeepNest()
      : func(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_int_type(32),
                                       {ctx.data->get_int_type(32)}))) {
    auto i32 = ctx.data->get_int_type(32);
    auto b = func.builder();
    auto entry = func.func->get_entry();
    for (BB *bb : {&h0, &h1, &h2, &x1, &x0, &sib, &x2}) {
      *bb = b.append_bb();
    }
    auto exit_bb = b.append_bb();

    b.at_end(entry);
    auto c = b.build_int_cmp(ValueR{entry->args[0]},
                             ValueR{ctx.data->get_constant_value(0, i32)},
                             foptim::fir::ICmpInstrSubType::NE);
    b.build_branch(h0);

    const auto latch = [&](BB from, BB back, BB out) {
      b.at_end(from);
      b.build_cond_branch(c, back, out);
    };
    b.at_end(h0);
    b.build_branch(h1);
    b.at_end(h1);
    b.build_branch(h2);
    latch(h2, h2, x1);
    latch(x1, h1, x0);
    b.at_end(x0);
    b.build_branch(sib);
    latch(sib, sib, x2);
    latch(x2, h0, exit_bb);

    b.at_end(exit_bb);
    b.build_return(ValueR{ctx.data->get_constant_value(0, i32)});
  }
};
}  // namespace

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

TEST(ScalarEvoTest, BuiltNodesShare) {
  using foptim::fir::ICmpInstrSubType;
  CountingLoop l{ICmpInstrSubType::SLT, 1, 10};
  LoopSetup s{*l.func.func};
  foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};

  // nodes made by the builders carry no value, equal ones are still one node
  auto n = evo.get(ValueR{l.func.func->get_entry()->args[0]});
  ASSERT_TRUE(evo.is_invariant(n));
  EXPECT_EQ(evo.mk_ext(n, true, 64), evo.mk_ext(n, true, 64));
  EXPECT_NE(evo.mk_ext(n, true, 64), evo.mk_ext(n, false, 64));
  const auto c3 = evo.mk_const(3, 32);
  EXPECT_EQ(evo.mk_add(n, c3), evo.mk_add(n, c3));
}

TEST(ScalarEvoTest, DerivedRecurrence) {
  // the back edge passes (i + 1) * 8 = {8,+,8} and d starts at 0, so d is 8 * i
  DerivedLoop l{0, false};
  LoopSetup s{*l.func.func};
  foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};
  auto d = evo.get(l.d_arg);
  ASSERT_TRUE(evo.is_const_addrec(d));
  EXPECT_EQ(evo.eval_at(d, 0), 0);
  EXPECT_EQ(evo.eval_at(d, 5), 40);
}

TEST(ScalarEvoTest, DerivedRecurrenceWrongStart) {
  // d is 3 in the first iteration and 8 * i after that, no recurrence
  DerivedLoop l{3, false};
  LoopSetup s{*l.func.func};
  foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};
  EXPECT_FALSE(evo.is_addrec(evo.get(l.d_arg)));
  EXPECT_FALSE(evo.is_invariant(evo.get(l.d_arg)));
}

TEST(ScalarEvoTest, DerivedRecurrenceNeedsNoWrap) {
  // sext(i + 1) of an i32 counter is only a recurrence of the wide type once
  // the counter is known not to wrap, then d is derived from it
  DerivedLoop l{0, true};
  LoopSetup s{*l.func.func};
  foptim::optim::ScalarEvo evo{s.cfg, s.loops.info[0]};
  EXPECT_FALSE(evo.is_addrec(evo.get(l.d_arg)));
  evo.assume_no_wrap(evo.get(l.i_next), true);
  auto d = evo.get(l.d_arg);
  ASSERT_TRUE(evo.is_const_addrec(d));
  EXPECT_EQ(evo.eval_at(d, 0), 0);
  EXPECT_EQ(evo.eval_at(d, 5), 40);
}

TEST(LoopNestingTest, ThreeDeepWithSibling) {
  ThreeDeepNest n;
  LoopSetup s{*n.func.func};
  ASSERT_EQ(s.loops.info.size(), 4);
  foptim::optim::LoopNesting nest{s.cfg, s.loops.info};
  ASSERT_EQ(nest.size(), 4);

  // the loops in the order LoopInfoAnalysis found them
  const auto loop_of = [&](foptim::fir::BasicBlock header) {
    const foptim::u32 id = s.cfg.get_bb_id(header);
    for (foptim::u32 l = 0; l < nest.size(); l++) {
      if (s.loops.info[l].head == id) {
        return l;
      }
    }
    ADD_FAILURE() << "no loop with that header";
    return ~0U;
  };
  const auto l0 = loop_of(n.h0);
  const auto l1 = loop_of(n.h1);
  const auto l2 = loop_of(n.h2);
  const auto l3 = loop_of(n.sib);

  EXPECT_EQ(nest.roots, (foptim::TVec<foptim::u32>{l0}));
  EXPECT_EQ(nest.parent[l0], -1);
  EXPECT_EQ(nest.parent[l1], static_cast<foptim::i32>(l0));
  EXPECT_EQ(nest.parent[l2], static_cast<foptim::i32>(l1));
  EXPECT_EQ(nest.parent[l3], static_cast<foptim::i32>(l0));
  EXPECT_EQ(nest.children[l0].size(), 2);
  EXPECT_EQ(nest.children[l1], (foptim::TVec<foptim::u32>{l2}));
  EXPECT_TRUE(nest.children[l2].empty());

  // the whole nest is nested in the outer loop, siblings are not in each other
  EXPECT_TRUE(nest.is_nested_in(l2, l0));
  EXPECT_TRUE(nest.is_nested_in(l2, l1));
  EXPECT_TRUE(nest.is_nested_in(l3, l0));
  EXPECT_FALSE(nest.is_nested_in(l0, l2));
  EXPECT_FALSE(nest.is_nested_in(l3, l1));
  EXPECT_FALSE(nest.is_nested_in(l1, l3));
  EXPECT_FALSE(nest.is_nested_in(l0, l0));

  // a block belongs to the innermost loop that contains it
  const auto own = [&](foptim::u32 l) {
    auto blocks = nest.own_blocks[l];
    std::ranges::sort(blocks);
    return blocks;
  };
  const auto ids = [&](std::initializer_list<foptim::fir::BasicBlock> bbs) {
    foptim::TVec<foptim::u32> out;
    for (auto bb : bbs) {
      out.push_back(s.cfg.get_bb_id(bb));
    }
    std::ranges::sort(out);
    return out;
  };
  EXPECT_EQ(own(l0), ids({n.h0, n.x0, n.x2}));
  EXPECT_EQ(own(l1), ids({n.h1, n.x1}));
  EXPECT_EQ(own(l2), ids({n.h2}));
  EXPECT_EQ(own(l3), ids({n.sib}));
  // the entry block is outside of every loop
  EXPECT_EQ(nest.innermost_of[s.cfg.entry], -1);
  EXPECT_EQ(nest.innermost_of[s.cfg.get_bb_id(n.h2)],
            static_cast<foptim::i32>(l2));
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
