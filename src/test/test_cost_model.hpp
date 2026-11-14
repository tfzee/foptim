#include <gtest/gtest.h>

#include "config/compiler_config.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function_ref.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/cost_model/aggregators.hpp"
#include "optim/cost_model/cost.hpp"
#include "optim/cost_model/fir_lowering_estimate.hpp"
#include "optim/cost_model/machine.hpp"

namespace {
using foptim::fir::ICmpInstrSubType;
using foptim::fir::ValueR;
namespace cm = foptim::optim::cost_model;

cm::Machine test_machine() {
  foptim::conf::Target target{};
  target.features = {};
  return cm::Machine::from_target(target);
}

cm::CostCtx ctx_for(const cm::Machine &m) {
  cm::CostCtx ctx;
  ctx.m = &m;
  return ctx;
}

// f(a, b): %t = a + b; ret %t
struct TwoArgAdd {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::FunctionR func;
  ValueR a;
  ValueR b;
  foptim::fir::Instr add_instr;

  explicit TwoArgAdd(bool immediate_rhs = false)
      : func(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_int_type(32),
                                       {ctx.data->get_int_type(32),
                                        ctx.data->get_int_type(32)}))) {
    auto i32 = ctx.data->get_int_type(32);
    auto builder = func.builder();
    auto entry = func.func->get_entry();
    builder.at_end(entry);
    a = ValueR{entry->args[0]};
    b = immediate_rhs ? ValueR{ctx.data->get_constant_value(5, i32)}
                       : ValueR{entry->args[1]};
    const ValueR sum = builder.build_int_add(a, b);
    add_instr = sum.as_instr();
    builder.build_return(sum);
  }
};

}  // namespace

TEST(CostModelTest, PlainAddIsAluRR) {
  TwoArgAdd f;
  const cm::Machine m = test_machine();
  cm::CostCtx ctx = ctx_for(m);
  const auto &row = m.table(cm::X86OpClass::ALU_RR);
  EXPECT_DOUBLE_EQ(cm::cost(f.add_instr, cm::CostKind::Latency, ctx).value,
                   row.latency);
  EXPECT_DOUBLE_EQ(cm::cost(f.add_instr, cm::CostKind::Throughput, ctx).value,
                   row.recip_throughput);
  EXPECT_DOUBLE_EQ(cm::cost(f.add_instr, cm::CostKind::Size, ctx).value,
                   row.size_bytes);
}

TEST(CostModelTest, ImmediateOperandSelectsAluRI) {
  TwoArgAdd f{/*immediate_rhs=*/true};
  const cm::Machine m = test_machine();
  cm::CostCtx ctx = ctx_for(m);
  ctx.operand_is_const[1] = true;
  const auto &row = m.table(cm::X86OpClass::ALU_RI);
  EXPECT_DOUBLE_EQ(cm::cost(f.add_instr, cm::CostKind::Size, ctx).value,
                   row.size_bytes);
}

TEST(CostModelTest, AddressArithmeticFoldsIntoLoadForFree) {
  foptim::fir::Context ctx{nullptr};
  auto i32 = ctx.data->get_int_type(32);
  auto ptr_ty = ctx.data->get_ptr_type();
  auto func = ctx.data->create_function(
      "f", ctx.data->get_func_ty(i32, {ptr_ty}));
  auto builder = func.builder();
  auto entry = func.func->get_entry();
  builder.at_end(entry);
  const ValueR base = ValueR{entry->args[0]};
  const ValueR disp = ValueR{ctx.data->get_constant_value(16, i32)};
  const ValueR addr = builder.build_ptr_add(base, disp);
  const ValueR loaded = builder.build_load(i32, addr, false, false);
  builder.build_return(loaded);

  const cm::Machine m = test_machine();
  cm::CostCtx addr_ctx = ctx_for(m);
  cm::detect_fold_context(addr.as_instr(), addr_ctx);
  EXPECT_TRUE(addr_ctx.folds_into_address);
  EXPECT_DOUBLE_EQ(
      cm::cost(addr.as_instr(), cm::CostKind::Latency, addr_ctx).value, 0.0);
  EXPECT_DOUBLE_EQ(
      cm::cost(addr.as_instr(), cm::CostKind::Size, addr_ctx).value, 0.0);

  // the load costs a LOAD, the address math is free
  cm::CostCtx load_ctx = ctx_for(m);
  cm::detect_fold_context(loaded.as_instr(), load_ctx);
  EXPECT_DOUBLE_EQ(
      cm::cost(loaded.as_instr(), cm::CostKind::Latency, load_ctx).value,
      m.table(cm::X86OpClass::LOAD).latency);
}

TEST(CostModelTest, FusableCompareFoldsIntoBranchButKeepsLatency) {
  foptim::fir::Context ctx{nullptr};
  auto i32 = ctx.data->get_int_type(32);
  auto func = ctx.data->create_function(
      "f", ctx.data->get_func_ty(i32, {i32, i32}));
  auto builder = func.builder();
  auto entry = func.func->get_entry();
  auto true_bb = builder.append_bb();
  auto false_bb = builder.append_bb();

  builder.at_end(entry);
  const ValueR a = ValueR{entry->args[0]};
  const ValueR b = ValueR{entry->args[1]};
  const ValueR cmp = builder.build_int_cmp(a, b, ICmpInstrSubType::SLT);
  const foptim::fir::Instr branch_instr =
      builder.build_cond_branch(cmp, true_bb, false_bb);

  builder.at_end(true_bb);
  builder.build_return(a);
  builder.at_end(false_bb);
  builder.build_return(b);

  const cm::Machine m = test_machine();
  cm::CostCtx cmp_ctx = ctx_for(m);
  cm::detect_fold_context(cmp.as_instr(), cmp_ctx);
  EXPECT_TRUE(cmp_ctx.folds_into_user);
  // latency is real
  EXPECT_DOUBLE_EQ(
      cm::cost(cmp.as_instr(), cm::CostKind::Latency, cmp_ctx).value,
      m.table(cm::X86OpClass::SETCC).latency);
  // but no separate uop or bytes
  EXPECT_DOUBLE_EQ(
      cm::cost(cmp.as_instr(), cm::CostKind::Throughput, cmp_ctx).value, 0.0);

  cm::CostCtx branch_ctx = ctx_for(m);
  EXPECT_DOUBLE_EQ(
      cm::cost(branch_instr, cm::CostKind::Latency, branch_ctx).value,
      m.table(cm::X86OpClass::CMP_JCC_FUSED).latency);
}

TEST(CostModelTest, ZExt32To64IsFreeButNarrowerIsNot) {
  foptim::fir::Context ctx{nullptr};
  auto i16 = ctx.data->get_int_type(16);
  auto i32 = ctx.data->get_int_type(32);
  auto i64 = ctx.data->get_int_type(64);
  auto func = ctx.data->create_function(
      "f", ctx.data->get_func_ty(i64, {i32, i16}));
  auto builder = func.builder();
  auto entry = func.func->get_entry();
  builder.at_end(entry);
  const ValueR from32 = builder.build_zext(ValueR{entry->args[0]}, i64);
  const ValueR from16 = builder.build_zext(ValueR{entry->args[1]}, i64);
  builder.build_return(from32);

  const cm::Machine m = test_machine();
  cm::CostCtx ctx32 = ctx_for(m);
  EXPECT_DOUBLE_EQ(
      cm::cost(from32.as_instr(), cm::CostKind::Size, ctx32).value, 0.0);

  cm::CostCtx ctx16 = ctx_for(m);
  EXPECT_GT(cm::cost(from16.as_instr(), cm::CostKind::Size, ctx16).value, 0.0);
}

TEST(CostModelTest, BlockThroughputIsCriticalPathForADependencyChain) {
  // 3 deep chain of ALU ops, the critical path dominates the resource bound
  foptim::fir::Context ctx{nullptr};
  auto i32 = ctx.data->get_int_type(32);
  auto func = ctx.data->create_function(
      "f", ctx.data->get_func_ty(i32, {i32, i32}));
  auto builder = func.builder();
  auto entry = func.func->get_entry();
  builder.at_end(entry);
  const ValueR a = ValueR{entry->args[0]};
  const ValueR b = ValueR{entry->args[1]};
  const ValueR t1 = builder.build_int_add(a, b);
  const ValueR t2 = builder.build_int_add(t1, b);
  const ValueR t3 = builder.build_int_add(t2, b);
  builder.build_return(t3);

  const cm::Machine m = test_machine();
  const cm::BlockThroughputCost bt{entry, m};
  const double alu_latency = m.table(cm::X86OpClass::ALU_RR).latency;
  EXPECT_DOUBLE_EQ(bt.critical_path(), 3 * alu_latency);
  EXPECT_LT(bt.resource_bound(), bt.critical_path());
  EXPECT_DOUBLE_EQ(bt.estimate(), bt.critical_path());
}

namespace {
// entry: br loop(0)
// loop(i): i1 = i + 1; c = i1 < limit; cond_br c, loop(i1), exit
// exit: ret i1
struct CountingLoopFixture {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::FunctionR func;
  foptim::fir::BasicBlock loop_bb{foptim::fir::BasicBlock::invalid()};

  explicit CountingLoopFixture(foptim::i64 limit)
      : func(ctx.data->create_function(
            "f", ctx.data->get_func_ty(ctx.data->get_int_type(32), {}))) {
    auto i32 = ctx.data->get_int_type(32);
    auto builder = func.builder();
    auto entry_bb = func.func->get_entry();
    loop_bb = builder.append_bb();
    loop_bb.add_arg(ctx.data->storage.insert_bb_arg(loop_bb, i32));
    auto exit_bb = builder.append_bb();

    builder.at_end(entry_bb);
    auto br = builder.build_branch(loop_bb);
    br.add_bb_arg(0, ValueR{ctx.data->get_constant_value(0, i32)});

    builder.at_end(loop_bb);
    const ValueR i_arg = ValueR{loop_bb->args[0]};
    const ValueR i_next = builder.build_int_add(
        i_arg, ValueR{ctx.data->get_constant_value(1, i32)});
    const ValueR limit_v = ValueR{ctx.data->get_constant_value(limit, i32)};
    const ValueR cond =
        builder.build_int_cmp(i_next, limit_v, ICmpInstrSubType::SLT);
    auto cbr = builder.build_cond_branch(cond, loop_bb, exit_bb);
    cbr.add_bb_arg(0, i_next);

    builder.at_end(exit_bb);
    builder.build_return(i_next);
  }
};
}  // namespace

TEST(LoopCostAnalysisTest, ConstantTripCountTimesPerIterationCost) {
  CountingLoopFixture l{10};
  foptim::optim::CFG cfg{*l.func.func};
  foptim::optim::Dominators dom{cfg};
  foptim::optim::LoopInfoAnalysis loops{dom};
  ASSERT_EQ(loops.info.size(), 1u);
  foptim::optim::ScalarEvo evo{cfg, loops.info[0]};

  const cm::Machine m = test_machine();
  cm::LoopCostAnalysis lc{cfg, dom, loops.info[0], evo, m};
  ASSERT_TRUE(lc.trip_count().has_value());
  EXPECT_DOUBLE_EQ(*lc.trip_count(), 10.0);
  EXPECT_DOUBLE_EQ(lc.total_cost(),
                   std::max(lc.iter_resource_bound(), lc.chain_latency()) *
                       10.0);
}

TEST(RegisterPressureEstimateTest, OverlappingLiveRangesCountedCorrectly) {
  // %t1 = a + b; %t2 = a + t1; %t3 = b + t2; ret %t3
  foptim::fir::Context ctx{nullptr};
  auto i32 = ctx.data->get_int_type(32);
  auto func = ctx.data->create_function(
      "f", ctx.data->get_func_ty(i32, {i32, i32}));
  auto builder = func.builder();
  auto entry = func.func->get_entry();
  builder.at_end(entry);
  const ValueR a = ValueR{entry->args[0]};
  const ValueR b = ValueR{entry->args[1]};
  const ValueR t1 = builder.build_int_add(a, b);
  const ValueR t2 = builder.build_int_add(a, t1);
  const ValueR t3 = builder.build_int_add(b, t2);
  builder.build_return(t3);

  const cm::RegisterPressureEstimate rp{entry};
  EXPECT_EQ(rp.max_pressure(), 3u);
}
