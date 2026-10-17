#include <gtest/gtest.h>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function_ref.hpp"
#include "mir/matcher.hpp"
#include "mir/matcher_pm.hpp"

namespace {
namespace pm = foptim::fmir::pm;
using foptim::fir::ValueR;

// f(p, x): m = x * 4; a = p + m; v = load a; ret v
struct IndexedLoad {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::FunctionR func;
  ValueR p;
  ValueR x;
  ValueR mul;
  ValueR add;
  ValueR load;

  explicit IndexedLoad(foptim::i64 scale = 4, bool swap_mul = false)
      : func(ctx.data->create_function(
            "f", ctx.data->get_func_ty(
                     ctx.data->get_int_type(32),
                     {ctx.data->get_ptr_type(), ctx.data->get_int_type(64)}))) {
    auto i32 = ctx.data->get_int_type(32);
    auto i64 = ctx.data->get_int_type(64);
    auto builder = func.builder();
    builder.at_end(func.func->get_entry());
    p = ValueR{func.func->get_entry()->args[0]};
    x = ValueR{func.func->get_entry()->args[1]};
    auto c = ValueR{ctx.data->get_constant_value(scale, i64)};
    mul = swap_mul ? builder.build_int_mul(c, x) : builder.build_int_mul(x, c);
    add = builder.build_ptr_add(p, mul);
    load = builder.build_load(i32, add, false, false);
    builder.build_return(load);
  }
};
}  // namespace

TEST(MatcherPMTest, BindsAndMatchesOperandPositions) {
  IndexedLoad f;
  foptim::fir::Instr add{foptim::fir::Instr::invalid()};
  foptim::fir::Instr mul{foptim::fir::Instr::invalid()};
  EXPECT_TRUE(pm::match(
      f.load.as_instr(),
      pm::Load(pm::capture(
          add, pm::PtrAdd(pm::Any, pm::capture(mul, pm::IntMul()))))));
  EXPECT_EQ(add, f.add.as_instr());
  EXPECT_EQ(mul, f.mul.as_instr());

  // the multiply is operand 1 of the add, not operand 0
  EXPECT_FALSE(pm::match(f.load.as_instr(),
                         pm::Load(pm::PtrAdd(pm::IntMul(), pm::Any))));
  // operand 0 of the add is a block argument, not an instruction
  EXPECT_FALSE(pm::match(f.load.as_instr(),
                         pm::Load(pm::PtrAdd(pm::PtrAdd(), pm::Any))));
  // wrong kind at the root
  EXPECT_FALSE(pm::match(f.add.as_instr(), pm::Load()));
}

TEST(MatcherPMTest, ConstantsAndPredicates) {
  IndexedLoad f{8};
  foptim::i128 scale = 0;
  auto is_scale = [](foptim::i128 v) {
    return v == 1 || v == 2 || v == 4 || v == 8;
  };
  EXPECT_TRUE(pm::match(f.mul.as_instr(),
                        pm::IntMul(pm::Any, pm::const_int(scale, is_scale))));
  EXPECT_EQ(scale, 8);
  EXPECT_TRUE(
      pm::match(f.mul.as_instr(), pm::IntMul(pm::Any, pm::const_int_is(8))));
  EXPECT_FALSE(
      pm::match(f.mul.as_instr(), pm::IntMul(pm::Any, pm::const_int_is(4))));
  // the constant is operand 1
  EXPECT_FALSE(
      pm::match(f.mul.as_instr(), pm::IntMul(pm::const_int(), pm::Any)));

  IndexedLoad g{3};
  EXPECT_FALSE(pm::match(g.mul.as_instr(),
                         pm::IntMul(pm::Any, pm::const_int(scale, is_scale))));
  // not written on a failed match
  EXPECT_EQ(scale, 8);
}

TEST(MatcherPMTest, Commutative) {
  IndexedLoad f{4, true};
  foptim::i128 scale = 0;
  EXPECT_FALSE(
      pm::match(f.mul.as_instr(), pm::IntMul(pm::Any, pm::const_int(scale))));
  EXPECT_TRUE(
      pm::match(f.mul.as_instr(),
                pm::commutative(pm::IntMul, pm::Any, pm::const_int(scale))));
  EXPECT_EQ(scale, 4);
}

TEST(MatcherPMTest, TypeAndWhere) {
  IndexedLoad f;
  auto is_ptr = [](foptim::fir::TypeR t) { return t->is_ptr(); };
  auto is_i32 = [](foptim::fir::TypeR t) {
    return t->is_int() && t->as_int() == 32;
  };
  EXPECT_TRUE(pm::match(f.load.as_instr(),
                        pm::Load(pm::of_type(is_ptr, pm::PtrAdd()))));
  EXPECT_FALSE(pm::match(f.load.as_instr(),
                         pm::Load(pm::of_type(is_i32, pm::PtrAdd()))));
  EXPECT_TRUE(pm::match(
      f.load.as_instr(),
      pm::Load(pm::where([](ValueR v) { return v.is_instr(); }, pm::Any))));
  EXPECT_TRUE(pm::match(
      f.mul.as_instr(),
      pm::IntMul(pm::either(pm::const_int(), pm::Any), pm::const_int())));
}

namespace {
bool never(foptim::fir::Instr, foptim::fmir::MatchResult &,
           foptim::fmir::ExtraMatchData &) {
  return false;
}
bool always(foptim::fir::Instr, foptim::fmir::MatchResult &,
            foptim::fmir::ExtraMatchData &) {
  return true;
}
}  // namespace

TEST(MatcherPMTest, RuleSetBuckets) {
  foptim::fmir::RuleSet rules;
  rules.add(pm::Load, never);
  rules.add(pm::IntMul, always);
  rules.add(pm::IntAdd, always);
  rules.add(pm::Load, always);

  EXPECT_EQ(rules.rules_for(foptim::fir::InstrType::LoadInstr).size(), 2);
  // IntMul and IntAdd are binary instructions with a subtype each
  const auto &bin = rules.rules_for(foptim::fir::InstrType::BinaryInstr);
  ASSERT_EQ(bin.size(), 2);
  EXPECT_EQ(bin[0].subtype,
            static_cast<foptim::u32>(foptim::fir::BinaryInstrSubType::IntMul));
  EXPECT_EQ(bin[1].subtype,
            static_cast<foptim::u32>(foptim::fir::BinaryInstrSubType::IntAdd));
  EXPECT_EQ(rules.rules_for(foptim::fir::InstrType::StoreInstr).size(), 0);
  // registration order is the priority
  EXPECT_EQ(rules.rules_for(foptim::fir::InstrType::LoadInstr)[0].generate,
            &never);
}
