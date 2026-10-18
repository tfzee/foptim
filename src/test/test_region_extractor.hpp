#include <gtest/gtest.h>

#include <array>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/interpreter/interpreter.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/helper/region_extractor.hpp"

namespace {
using foptim::fir::ICmpInstrSubType;
using foptim::fir::ValueR;
using foptim::optim::ExtractAttribs;
using OutlineRegion = foptim::optim::Region;
using foptim::optim::RegionReject;

// f(a, b): if (a == 0) return 100; <rest>
// caller(): f(5, 3) + f(0, 9) * 1000
struct RegionModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR i32{ctx.data->get_int_type(32)};
  std::array<foptim::fir::TypeR, 2> pair{i32, i32};
  foptim::fir::FunctionR f;
  foptim::fir::FunctionR caller;
  foptim::fir::BasicBlock rest{foptim::fir::BasicBlock::invalid()};

  ValueR c(foptim::i64 v) {
    return ValueR{ctx.data->get_constant_value(v, i32)};
  }
  ValueR arg(foptim::fir::BasicBlock bb, size_t i) {
    return ValueR{bb->args[i]};
  }

  explicit RegionModule(bool pair_result = false)
      : f(ctx.data->create_function(
            "f", pair_result ? ctx.data->get_func_ty(pair, {i32, i32})
                             : ctx.data->get_func_ty(i32, {i32, i32}))),
        caller(ctx.data->create_function("caller",
                                         ctx.data->get_func_ty(i32, {}))) {
    auto b = f.builder();
    auto entry = f.func->get_entry();
    rest = b.append_bb();
    auto early = b.append_bb();
    b.at_end(entry);
    auto cond = b.build_int_cmp(arg(entry, 0), c(0), ICmpInstrSubType::EQ);
    b.build_cond_branch(cond, early, rest);
    b.at_end(early);
    if (pair_result) {
      std::array<ValueR, 2> rets{c(100), c(101)};
      b.build_return(rets);
    } else {
      b.build_return(c(100));
    }
  }

  void finish_caller() {
    auto cb = caller.builder();
    cb.at_end(caller.func->get_entry());
    auto fv = ValueR{ctx.data->get_constant_value(f)};
    std::array<ValueR, 2> a1{c(5), c(3)};
    std::array<ValueR, 2> a2{c(0), c(9)};
    auto ret_tys = f.func->func_ty->as_func().return_types;
    auto r1 = cb.build_call(fv, f.func->func_ty, ret_tys, a1);
    auto r2 = cb.build_call(fv, f.func->func_ty, ret_tys, a2);
    auto v1 = r1.result(0);
    auto v2 = r2.result(0);
    if (ret_tys.size() == 2) {
      v1 = cb.build_int_add(v1, r1.result(1));
      v2 = cb.build_int_add(v2, r2.result(1));
    }
    cb.build_return(cb.build_int_add(v1, cb.build_int_mul(v2, c(1000))));
  }

  foptim::i128 run() {
    foptim::fir::intepreter::Interpreter interp{caller.func};
    EXPECT_TRUE(interp.run());
    return interp.return_values()[0].as_int();
  }

  std::optional<OutlineRegion> find(foptim::fir::BasicBlock entry,
                                    RegionReject *why = nullptr,
                                    foptim::optim::RegionLimits lim = {}) {
    foptim::optim::CFG cfg{*f.func};
    return foptim::optim::find_region(*f.func, entry, cfg, lim, why);
  }
};
}  // namespace

TEST(RegionExtractor, StraightLineReturns) {
  RegionModule m;
  auto b = m.f.builder();
  b.at_end(m.rest);
  auto a = m.arg(m.f.func->get_entry(), 0);
  auto bb = m.arg(m.f.func->get_entry(), 1);
  auto x = b.build_int_mul(a, bb);
  auto y = b.build_int_add(x, m.c(7));
  b.build_return(b.build_int_sub(y, a));
  m.finish_caller();
  const auto expected = m.run();
  ASSERT_TRUE(m.ctx.data->verify());

  auto r = m.find(m.rest);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->kind, OutlineRegion::Kind::Returns);
  EXPECT_EQ(r->n_instrs, 4);
  ASSERT_EQ(r->live_in.size(), 2);
  EXPECT_TRUE(r->live_in[0] == a);
  EXPECT_TRUE(r->live_in[1] == bb);

  auto call =
      foptim::optim::extract_region(*m.f.func, *r, "f.rest", ExtractAttribs{});
  EXPECT_TRUE(m.ctx.data->verify());
  EXPECT_EQ(m.run(), expected);
  EXPECT_EQ(call->args.size(), 3);
  EXPECT_EQ(m.rest->instructions.size(), 2);
  EXPECT_EQ(m.f.func->n_bbs(), 3);
  auto nf = m.ctx.data->get_function("f.rest");
  EXPECT_TRUE(nf->attribs.no_inline);
  EXPECT_TRUE(nf->attribs.outlined);
  EXPECT_FALSE(nf->attribs.no_return);
}

TEST(RegionExtractor, LoopThroughEntryBlockArgsAndDiamond) {
  RegionModule m;
  auto b = m.f.builder();
  auto entry = m.f.func->get_entry();
  // rest(i): n = i + 1; if (n < b) { t = n * a } else { t = n + a }; if (n <
  // 10) rest(t) else ret t
  m.rest.add_arg(m.ctx.data->storage.insert_bb_arg(m.rest, m.i32));
  auto then_bb = b.append_bb();
  auto else_bb = b.append_bb();
  auto join = b.append_bb();
  auto done = b.append_bb();
  join.add_arg(m.ctx.data->storage.insert_bb_arg(join, m.i32));
  // the entry branch passes a value to the new block argument
  {
    auto term = entry->instructions.back();
    ASSERT_EQ(term->bbs.size(), 2);
    term.add_bb_arg(1, m.c(1));
  }
  b.at_end(m.rest);
  auto n = b.build_int_add(m.arg(m.rest, 0), m.c(1));
  auto cmp = b.build_int_cmp(n, m.arg(entry, 1), ICmpInstrSubType::SLT);
  b.build_cond_branch(cmp, then_bb, else_bb);
  b.at_end(then_bb);
  auto tv = b.build_int_mul(n, m.arg(entry, 0));
  b.build_branch(join).add_bb_arg(0, tv);
  b.at_end(else_bb);
  auto ev = b.build_int_add(n, m.arg(entry, 0));
  b.build_branch(join).add_bb_arg(0, ev);
  b.at_end(join);
  auto lt = b.build_int_cmp(n, m.c(10), ICmpInstrSubType::SLT);
  auto cbr = b.build_cond_branch(lt, m.rest, done);
  cbr.add_bb_arg(0, m.arg(join, 0));
  b.at_end(done);
  b.build_return(m.arg(join, 0));
  m.finish_caller();
  ASSERT_TRUE(m.ctx.data->verify());
  const auto expected = m.run();

  auto r = m.find(m.rest);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->blocks.size(), 5);
  EXPECT_TRUE(r->blocks[0] == m.rest);
  // function arguments a, b are the only live ins
  EXPECT_EQ(r->live_in.size(), 2);
  foptim::optim::extract_region(*m.f.func, *r, "f.rest", ExtractAttribs{});
  EXPECT_TRUE(m.ctx.data->verify());
  EXPECT_EQ(m.run(), expected);
  EXPECT_EQ(m.f.func->n_bbs(), 3);
}

TEST(RegionExtractor, LiveInKinds) {
  RegionModule m;
  auto b = m.f.builder();
  auto entry = m.f.func->get_entry();
  // an instruction result of the entry block and a block argument of a
  // middle block are live in, constants are not
  auto mid = b.append_bb();
  mid.add_arg(m.ctx.data->storage.insert_bb_arg(mid, m.i32));
  // rewrite: entry cond_br goes to mid instead of rest
  {
    auto term = entry->instructions.back();
    term.replace_bb(1, mid);
  }
  b.at_end(entry);
  b.at_penultimate(entry);
  auto in_entry = b.build_int_add(m.arg(entry, 1), m.c(11));
  entry->instructions.back().add_bb_arg(1, in_entry);
  b.at_end(mid);
  b.build_branch(m.rest);
  b.at_end(m.rest);
  auto s = b.build_int_add(in_entry, m.arg(mid, 0));
  b.build_return(b.build_int_add(s, m.c(3)));
  m.finish_caller();
  ASSERT_TRUE(m.ctx.data->verify());
  const auto expected = m.run();

  auto r = m.find(mid);
  ASSERT_TRUE(r.has_value());
  // in_entry is used by the region, the block argument of mid is a
  // parameter of the entry and no live in
  ASSERT_EQ(r->live_in.size(), 1);
  EXPECT_TRUE(r->live_in[0] == in_entry);
  foptim::optim::extract_region(*m.f.func, *r, "f.rest", ExtractAttribs{});
  EXPECT_TRUE(m.ctx.data->verify());
  EXPECT_EQ(m.run(), expected);
  EXPECT_EQ(
      m.ctx.data->get_function("f.rest")->func_ty->as_func().arg_types.size(),
      2);
}

TEST(RegionExtractor, MultiResultReturn) {
  RegionModule m{true};
  auto b = m.f.builder();
  b.at_end(m.rest);
  auto a = m.arg(m.f.func->get_entry(), 0);
  auto bb = m.arg(m.f.func->get_entry(), 1);
  std::array<ValueR, 2> rets{b.build_int_add(a, bb), b.build_int_sub(a, bb)};
  b.build_return(rets);
  m.finish_caller();
  ASSERT_TRUE(m.ctx.data->verify());
  const auto expected = m.run();
  auto r = m.find(m.rest);
  ASSERT_TRUE(r.has_value());
  auto call =
      foptim::optim::extract_region(*m.f.func, *r, "f.rest", ExtractAttribs{});
  EXPECT_EQ(call->n_results(), 2);
  EXPECT_TRUE(m.ctx.data->verify());
  EXPECT_EQ(m.run(), expected);
}

TEST(RegionExtractor, StaticAllocaMovesAndNoReturnKind) {
  RegionModule m;
  auto b = m.f.builder();
  b.at_end(m.rest);
  auto buf = b.build_alloca(m.c(8));
  b.build_store(buf, m.arg(m.f.func->get_entry(), 1), false, false);
  b.build_unreach();
  m.finish_caller();
  auto r = m.find(m.rest);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->kind, OutlineRegion::Kind::NoReturn);
  EXPECT_EQ(r->live_in.size(), 1);
  auto call = foptim::optim::extract_region(*m.f.func, *r, "f.cold",
                                            ExtractAttribs{.cold = true});
  EXPECT_TRUE(m.ctx.data->verify());
  auto nf = m.ctx.data->get_function("f.cold");
  EXPECT_TRUE(nf->attribs.no_return);
  EXPECT_TRUE(nf->attribs.cold);
  EXPECT_TRUE(nf->func_ty->as_func().return_type()->is_void());
  EXPECT_TRUE(
      m.rest->instructions.back()->is(foptim::fir::InstrType::Unreachable));
  EXPECT_TRUE(call->get_parent() == m.rest);
  // the alloca is in the new function now
  bool found = false;
  for (auto bb : nf->basic_blocks) {
    for (auto i : bb->instructions) {
      found |= i->is(foptim::fir::InstrType::AllocaInstr);
    }
  }
  EXPECT_TRUE(found);
}

TEST(RegionExtractor, Rejects) {
  {
    // second entry: a block outside jumps into the middle of the region
    RegionModule m;
    auto b = m.f.builder();
    auto entry = m.f.func->get_entry();
    auto inner = b.append_bb();
    b.at_end(m.rest);
    b.build_branch(inner);
    b.at_end(inner);
    b.build_return(m.c(1));
    // early also jumps to inner
    auto early = entry->instructions.back()->bbs[0].bb;
    early->instructions.back().destroy();
    b.at_end(early);
    b.build_branch(inner);
    RegionReject why{};
    EXPECT_FALSE(m.find(m.rest, &why).has_value());
    EXPECT_EQ(why, RegionReject::SecondEntry);
  }
  {
    RegionModule m;
    RegionReject why{};
    auto b = m.f.builder();
    b.at_end(m.rest);
    b.build_return(m.c(1));
    EXPECT_FALSE(m.find(m.f.func->get_entry(), &why).has_value());
    EXPECT_EQ(why, RegionReject::FunctionEntry);
    EXPECT_FALSE(m.find(m.rest, &why, {.min_instrs = 5}).has_value());
    EXPECT_EQ(why, RegionReject::TooSmall);
  }
  {
    // dynamic alloca
    RegionModule m;
    auto b = m.f.builder();
    b.at_end(m.rest);
    auto p = b.build_alloca(m.arg(m.f.func->get_entry(), 1));
    b.build_store(p, m.c(1), false, false);
    b.build_return(m.c(1));
    RegionReject why{};
    EXPECT_FALSE(m.find(m.rest, &why).has_value());
    EXPECT_EQ(why, RegionReject::DynamicAlloca);
  }
  {
    // too many live ins
    RegionModule m;
    auto b = m.f.builder();
    b.at_end(m.rest);
    auto entry = m.f.func->get_entry();
    b.build_return(b.build_int_add(m.arg(entry, 0), m.arg(entry, 1)));
    RegionReject why{};
    EXPECT_FALSE(m.find(m.rest, &why, {.max_live_ins = 1}).has_value());
    EXPECT_EQ(why, RegionReject::TooManyLiveIns);
  }
  {
    // a promotable alloca of the function is used inside
    RegionModule m;
    auto b = m.f.builder();
    auto entry = m.f.func->get_entry();
    b.at_penultimate(entry);
    auto buf = b.build_alloca(m.c(4));
    b.build_store(buf, m.arg(entry, 1), false, false);
    b.at_end(m.rest);
    b.build_return(b.build_load(m.i32, buf, false, false));
    RegionReject why{};
    EXPECT_FALSE(m.find(m.rest, &why).has_value());
    EXPECT_EQ(why, RegionReject::PromotableAlloca);
  }
}

TEST(RegionExtractor, VoidFunction) {
  foptim::fir::Context ctx{nullptr};
  auto i32 = ctx.data->get_int_type(32);
  auto ptr = ctx.data->get_ptr_type();
  auto f = ctx.data->create_function(
      "v", ctx.data->get_func_ty(ctx.data->get_void_type(), {ptr, i32}));
  auto b = f.builder();
  auto entry = f.func->get_entry();
  auto rest = b.append_bb();
  auto early = b.append_bb();
  b.at_end(entry);
  auto cond = b.build_int_cmp(ValueR{entry->args[1]},
                              ValueR{ctx.data->get_constant_value(0, i32)},
                              ICmpInstrSubType::EQ);
  b.build_cond_branch(cond, early, rest);
  b.at_end(early);
  b.build_return();
  b.at_end(rest);
  b.build_store(ValueR{entry->args[0]}, ValueR{entry->args[1]}, false, false);
  b.build_return();
  ASSERT_TRUE(ctx.data->verify());
  foptim::optim::CFG cfg{*f.func};
  auto r = foptim::optim::find_region(*f.func, rest, cfg, {});
  ASSERT_TRUE(r.has_value());
  auto call =
      foptim::optim::extract_region(*f.func, *r, "v.rest", ExtractAttribs{});
  EXPECT_TRUE(ctx.data->verify());
  EXPECT_EQ(call->n_results(), 0);
  EXPECT_EQ(rest->instructions.size(), 2);
  EXPECT_FALSE(rest->instructions.back()->has_args());
}
