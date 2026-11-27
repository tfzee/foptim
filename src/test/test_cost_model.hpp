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
#include "optim/cost_model/branch_cost.hpp"
#include "optim/cost_model/cost.hpp"
#include "optim/cost_model/fir_lowering_estimate.hpp"
#include "optim/cost_model/machine.hpp"
#include "optim/cost_model/op_desc.hpp"

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

TEST(CostModelMachine, CacheHierarchy) {
  foptim::conf::Target zen{};
  zen.tune = "zen4";
  const cm::Machine m = cm::Machine::from_target(zen);
  EXPECT_EQ(m.cache(0).size, 32768u);
  EXPECT_EQ(m.cache(1).size, 1048576u);
  EXPECT_EQ(m.cache(2).size, 33554432u);
  double prev = 0.0;
  for (u32 l = 0; l < cm::kNumCacheLevels; l++) {
    EXPECT_EQ(m.cache(l).line_size, 64u);
    EXPECT_GT(m.cache(l).latency, prev);
    prev = m.cache(l).latency;
  }
  EXPECT_GT(m.mem_latency(), prev);
}

TEST(CostModelMachine, CacheOverrideKeepsLatencies) {
  foptim::conf::Target target{};
  const double table_l2_latency = cm::Machine::from_target(target).cache(1).latency;
  target.cache[1].size = 2097152;
  target.cache[1].assoc = 16;
  target.cache[2].size = 16777216;
  const cm::Machine m = cm::Machine::from_target(target);
  EXPECT_EQ(m.cache(0).size, 32768u);
  EXPECT_EQ(m.cache(1).size, 2097152u);
  EXPECT_EQ(m.cache(1).assoc, 16u);
  EXPECT_EQ(m.cache(1).line_size, 64u);
  EXPECT_EQ(m.cache(1).latency, table_l2_latency);
  EXPECT_EQ(m.cache(2).size, 16777216u);
}

TEST(CostModelMachine, TuneSelectsTables) {
  foptim::conf::Target target{};
  // no tune means the generic profile
  EXPECT_EQ(cm::Machine::from_target(target).arch, cm::MicroArch::Generic);
  for (const std::string_view name :
       {"generic", "zen4", "goldencove", "lioncove"}) {
    target.tune = name;
    const auto m = cm::Machine::from_target(target);
    EXPECT_EQ(cm::microarch_name(m.arch), name);
    EXPECT_EQ(cm::parse_microarch(name), m.arch);
  }
  EXPECT_FALSE(cm::parse_microarch("skylake").has_value());
  target.tune = "lioncove";
  EXPECT_EQ(cm::Machine::from_target(target).cache(1).size, 3145728u);
}

TEST(CostModelMachine, EveryProfileIsCompleteAndPlausible) {
  for (const auto arch : cm::all_microarchs()) {
    foptim::conf::Target target{};
    target.tune = std::string(cm::microarch_name(arch));
    const auto m = cm::Machine::from_target(target);
    SCOPED_TRACE(cm::microarch_name(arch));
    for (size_t i = 0; i < static_cast<size_t>(cm::X86OpClass::COUNT); i++) {
      const auto &row = m.table(static_cast<cm::X86OpClass>(i));
      SCOPED_TRACE(i);
      EXPECT_GT(row.recip_throughput, 0.0);
      EXPECT_GE(row.latency, 0.0);
      EXPECT_NE(row.ports_mask, 0);
      EXPECT_GT(row.size_bytes, 0);
    }
    const auto &c = m.core();
    EXPECT_GT(c.issue_width, 0u);
    EXPECT_GE(c.retire_width, c.issue_width);
    EXPECT_GT(c.n_alu_ports, 0u);
    EXPECT_GT(c.load_ports, 0u);
    EXPECT_GT(c.store_ports, 0u);
    EXPECT_GT(c.mispredict_penalty, 0u);
    EXPECT_GT(c.n_gp_regs, 0u);
    EXPECT_GT(c.n_vec_regs, 0u);
    EXPECT_GE(c.zmm_parts, 1u);
    EXPECT_GT(m.tune().size_weight, 0.0);
    for (u32 l = 0; l < cm::kNumCacheLevels; l++) {
      EXPECT_GT(m.cache(l).size, 0u);
      EXPECT_GT(m.cache(l).latency, 0.0);
    }
    EXPECT_GT(m.mem_latency(), 0.0);
  }
}

TEST(CostModelMachine, ProfilesFallBackToGeneric) {
  foptim::conf::Target target{};
  const auto generic = cm::Machine::from_target(target);
  target.tune = "goldencove";
  const auto gc = cm::Machine::from_target(target);
  // goldencove leaves its tune file out and the keys it does not set
  EXPECT_EQ(gc.tune().size_weight, generic.tune().size_weight);
  EXPECT_EQ(gc.core().taken_branch_cost, generic.core().taken_branch_cost);
  EXPECT_NE(gc.core().issue_width, generic.core().issue_width);
}

namespace {
double op_tput(const cm::Machine &m, cm::OpKind kind, u32 bits, u32 lanes,
               bool is_float = false) {
  return cm::cost(cm::OpDesc{.kind = kind,
                             .elem_bits = bits,
                             .lanes = lanes,
                             .is_float = is_float},
                  cm::CostKind::Throughput, m)
      .value;
}

TEST(CostModelOpDesc, VectorFamiliesAreDistinct) {
  const auto m = test_machine();
  // a packed multiply is slower than an add, a divide slower than both
  EXPECT_GT(op_tput(m, cm::OpKind::IntMul, 32, 8),
            op_tput(m, cm::OpKind::IntAlu, 32, 8));
  EXPECT_GT(op_tput(m, cm::OpKind::FloatDiv, 32, 8, true),
            op_tput(m, cm::OpKind::FloatMul, 32, 8, true));
  // a gather of eight lanes costs more than a plain load
  EXPECT_GT(op_tput(m, cm::OpKind::Gather, 32, 8),
            op_tput(m, cm::OpKind::Load, 32, 8));
}

TEST(CostModelOpDesc, WideVectorsPayPerPart) {
  const auto m = test_machine();
  // no avx512: a 512 bit op is two ymm ops
  EXPECT_DOUBLE_EQ(op_tput(m, cm::OpKind::IntAlu, 32, 16),
                   2 * op_tput(m, cm::OpKind::IntAlu, 32, 8));
}

TEST(CostModelOpDesc, Int64MulNeedsAvx512dqOrEmulation) {
  // the machine points at the features of its target
  foptim::conf::Target plain_target{};
  plain_target.features = {};
  foptim::conf::Target dq_target{};
  dq_target.features = {};
  dq_target.features.avx512dq = true;
  const auto plain = cm::Machine::from_target(plain_target);
  const auto dq = cm::Machine::from_target(dq_target);
  EXPECT_GT(op_tput(plain, cm::OpKind::IntMul, 64, 4),
            op_tput(dq, cm::OpKind::IntMul, 64, 4));
}

TEST(CostModelOpDesc, ScalarFormUsesScalarRows) {
  const auto m = test_machine();
  EXPECT_DOUBLE_EQ(op_tput(m, cm::OpKind::IntAlu, 64, 1),
                   m.table(cm::X86OpClass::ALU_RR).recip_throughput);
}
TEST(CostModelBranch, MispredictRateIsTheMinoritySide) {
  EXPECT_DOUBLE_EQ(cm::mispredict_rate(0.5), 0.5);
  EXPECT_DOUBLE_EQ(cm::mispredict_rate(0.1), 0.1);
  EXPECT_DOUBLE_EQ(cm::mispredict_rate(0.9), 0.1);
  EXPECT_DOUBLE_EQ(cm::mispredict_rate(1.0), 0.0);
}

TEST(CostModelBranch, IfConvertPicksByArmCost) {
  const auto m = test_machine();
  cm::IfConvertInput in;
  in.n_int_selects = 1;
  // a short arm is cheaper than a mispredict at 50 percent
  in.arm0 = {.tput = 1.0, .latency = 1.0};
  EXPECT_TRUE(cm::cost_if_convert(in, m).prefer_select());
  // a long arm is not
  in.arm0 = {.tput = 40.0, .latency = 52.0};
  EXPECT_FALSE(cm::cost_if_convert(in, m).prefer_select());
  // unless the branch is almost never mispredicted and the arm almost never
  // taken
  in.p0 = 0.01;
  EXPECT_FALSE(cm::cost_if_convert(in, m).prefer_select());
}

TEST(CostModelBranch, CarriedLatencyFavorsTheBranch) {
  const auto m = test_machine();
  cm::IfConvertInput in;
  in.n_int_selects = 1;
  // a one sided arm with a long latency, cheap in throughput
  in.arm0 = {.tput = 2.0, .latency = 14.0};
  in.p0 = 0.5;
  EXPECT_TRUE(cm::cost_if_convert(in, m).prefer_select());
  in.carried = true;
  const auto c = cm::cost_if_convert(in, m);
  EXPECT_GT(c.select, 14.0);
  // the branch only pays the arm when it is taken
  EXPECT_LT(c.branch - cm::mispredict_rate(in.p0) *
                           m.core().mispredict_penalty,
            c.select);
}

}  // namespace
