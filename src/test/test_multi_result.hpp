#include <gtest/gtest.h>

#include <array>
#include <unordered_set>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/interpreter/interpreter.hpp"
#include "ir/value.hpp"
#include "optim/func_passes/dce.hpp"
#include "optim/func_passes/lvn.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/func_passes/simplify_cfg.hpp"
#include "optim/helper/inline.hpp"
#include "config/compiler_config.hpp"
#include "mir/matcher.hpp"
#include "mir/optim/mir_pipeline.hpp"
#include "utils/job_system.hpp"
#include "mir/optim/calling_conv.hpp"

namespace {
using foptim::fir::ValueR;

// pair(a, b) -> [i32 i32]: ret a + b, a - b
// caller() -> i32: r = pair(7, 3); ret r.0 * ... r.0 - r.1 (10 - 4 = 6)
struct PairModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR i32{ctx.data->get_int_type(32)};
  std::array<foptim::fir::TypeR, 2> res_types{i32, i32};
  foptim::fir::FunctionR pair;
  foptim::fir::FunctionR caller;
  foptim::fir::Instr call{foptim::fir::Instr::invalid()};

  PairModule()
      : pair(ctx.data->create_function(
            "pair", ctx.data->get_func_ty(res_types, {i32, i32}))),
        caller(ctx.data->create_function("caller",
                                         ctx.data->get_func_ty(i32, {}))) {
    auto b = pair.builder();
    auto entry = pair.func->get_entry();
    b.at_end(entry);
    auto a0 = ValueR{entry->args[0]};
    auto a1 = ValueR{entry->args[1]};
    std::array<ValueR, 2> rets{b.build_int_add(a0, a1),
                               b.build_int_sub(a0, a1)};
    b.build_return(rets);

    auto cb = caller.builder();
    cb.at_end(caller.func->get_entry());
    std::array<ValueR, 2> args{ValueR{ctx.data->get_constant_value(7, i32)},
                               ValueR{ctx.data->get_constant_value(3, i32)}};
    call = cb.build_call(ValueR{ctx.data->get_constant_value(pair)},
                         pair.func->func_ty, res_types, args);
    cb.build_return(cb.build_int_sub(call.result(0), call.result(1)));
  }
};
}  // namespace

TEST(MultiResult, FunctionTypeInterning) {
  PairModule m;
  auto &d = *m.ctx.data;
  auto two = d.get_func_ty(m.res_types, {m.i32, m.i32});
  EXPECT_EQ(two, m.pair.func->func_ty);
  EXPECT_EQ(two->as_func().n_results(), 2);
  auto one = d.get_func_ty(m.i32, {m.i32, m.i32});
  EXPECT_NE(one, two);
  EXPECT_EQ(one->as_func().n_results(), 1);
  EXPECT_EQ(d.get_func_ty(d.get_void_type(), {})->as_func().n_results(), 0);
}

TEST(MultiResult, CallResults) {
  PairModule m;
  ASSERT_EQ(m.call->n_results(), 2);
  EXPECT_EQ(m.call->result_type(0), m.i32);
  EXPECT_EQ(m.call->result_type(1), m.i32);
  EXPECT_EQ(m.call->result_types().size(), 2);
  auto r0 = m.call.result(0);
  auto r1 = m.call.result(1);
  EXPECT_FALSE(r0 == r1);
  EXPECT_TRUE(r0 == m.call.result(0));
  EXPECT_EQ(r1.get_type(), m.i32);
  EXPECT_EQ(r1.result_idx(), 1);
  std::unordered_set<ValueR> set{r0, r1, m.call.result(0)};
  EXPECT_EQ(set.size(), 2);
}

TEST(MultiResult, Verifier) {
  PairModule m;
  EXPECT_TRUE(m.ctx.data->verify());
}

TEST(MultiResult, Printer) {
  PairModule m;
  EXPECT_EQ(fmt::format("{}", m.call.result(0))
                .substr(fmt::format("{}", m.call.result(0)).size() - 2),
            ".0");
  EXPECT_EQ(fmt::format("{}", m.call.result(1))
                .substr(fmt::format("{}", m.call.result(1)).size() - 2),
            ".1");
  // single result instructions print as before
  auto sub = m.caller.func->get_entry()->instructions[1];
  EXPECT_EQ(fmt::format("{}", sub.result()).find('.'), std::string::npos);
  auto call_str = fmt::format("{}", m.call);
  EXPECT_NE(call_str.find("[i32 i32]"), std::string::npos) << call_str;
}

TEST(MultiResult, Interpreter) {
  PairModule m;
  foptim::fir::intepreter::Interpreter interp{m.caller.func};
  ASSERT_TRUE(interp.run());
  ASSERT_EQ(interp.return_values().size(), 1);
  EXPECT_EQ(interp.return_values()[0].as_int(), 6);
  EXPECT_EQ(interp.get_value(m.call.result(0))->as_int(), 10);
  EXPECT_EQ(interp.get_value(m.call.result(1))->as_int(), 4);
}

TEST(MultiResult, VerifierRejectsWrongReturn) {
  PairModule m;
  // a function with one result that returns two values
  auto f = m.ctx.data->create_function("bad",
                                       m.ctx.data->get_func_ty(m.i32, {m.i32}));
  auto b = f.builder();
  b.at_end(f.func->get_entry());
  auto a = ValueR{f.func->get_entry()->args[0]};
  std::array<ValueR, 2> rets{a, a};
  b.build_return(rets);
  EXPECT_FALSE(m.ctx.data->verify());
}

TEST(MultiResult, ReplaceUsesPerResult) {
  PairModule m;
  EXPECT_EQ(m.call.result(0).get_n_uses(), 1);
  EXPECT_EQ(m.call.result(1).get_n_uses(), 1);
  m.call.result(1).replace_all_uses(
      ValueR{m.ctx.data->get_constant_value(1, m.i32)});
  EXPECT_EQ(m.call.result(0).get_n_uses(), 1);
  EXPECT_EQ(m.call.result(1).get_n_uses(), 0);
  EXPECT_TRUE(m.ctx.data->verify());
  foptim::fir::intepreter::Interpreter interp{m.caller.func};
  ASSERT_TRUE(interp.run());
  EXPECT_EQ(interp.return_values()[0].as_int(), 9);
}

TEST(MultiResult, Inline) {
  PairModule m;
  ASSERT_TRUE(foptim::optim::inline_call(m.call));
  EXPECT_TRUE(m.ctx.data->verify());
  foptim::fir::intepreter::Interpreter interp{m.caller.func};
  ASSERT_TRUE(interp.run());
  EXPECT_EQ(interp.return_values()[0].as_int(), 6);
}

TEST(MultiResult, FunctionPasses) {
  PairModule m;
  foptim::optim::SCCP sccp;
  foptim::optim::LVN lvn;
  foptim::optim::SimplifyCFG cfg;
  foptim::optim::DCE dce;
  for (auto *f : {m.caller.func, m.pair.func}) {
    sccp.apply(m.ctx, *f);
    lvn.apply(m.ctx, *f);
    cfg.apply(m.ctx, *f);
    dce.apply(m.ctx, *f);
  }
  EXPECT_TRUE(m.ctx.data->verify());
  foptim::fir::intepreter::Interpreter interp{m.caller.func};
  ASSERT_TRUE(interp.run());
  EXPECT_EQ(interp.return_values()[0].as_int(), 6);
}

// Backend: a callee returning its arguments and a caller returning the last
// result of a call to it. Result i has to end up in the next return register of
// its class.
namespace {
struct MixedModule {
  foptim::fir::Context ctx{nullptr};
  foptim::fmir::MFunc callee_mir;
  foptim::fmir::MFunc caller_mir;
  foptim::fir::Function *callee_f = nullptr;
  foptim::fir::Function *caller_f = nullptr;

  // kinds: i is i64, d is f64
  explicit MixedModule(std::string_view kinds) {
    std::vector<foptim::fir::TypeR> tys;
    for (char k : kinds) {
      tys.push_back(k == 'i' ? foptim::fir::TypeR{ctx.data->get_int_type(64)}
                             : foptim::fir::TypeR{ctx.data->get_float_type(64)});
    }
    auto callee = ctx.data->create_function(
        "callee", ctx.data->get_func_ty(
                      std::span<const foptim::fir::TypeR>{tys},
                      foptim::IRVec<foptim::fir::TypeR>(tys.begin(), tys.end())));
    auto caller = ctx.data->create_function(
        "caller", ctx.data->get_func_ty(
                      tys.back(),
                      foptim::IRVec<foptim::fir::TypeR>(tys.begin(), tys.end())));
    {
      auto b = callee.builder();
      auto entry = callee.func->get_entry();
      b.at_end(entry);
      std::vector<ValueR> rets;
      for (auto arg : entry->args) {
        rets.push_back(ValueR{arg});
      }
      b.build_return(rets);
    }
    auto b = caller.builder();
    auto entry = caller.func->get_entry();
    b.at_end(entry);
    std::vector<ValueR> args;
    for (auto arg : entry->args) {
      args.push_back(ValueR{arg});
    }
    auto call = b.build_call(ValueR{ctx.data->get_constant_value(callee)},
                             callee.func->func_ty, tys, args);
    b.build_return(call.result(static_cast<foptim::u8>(tys.size() - 1)));
    EXPECT_TRUE(ctx.data->verify());
    callee_f = callee.func;
    caller_f = caller.func;

    foptim::conf::CompConf conf;
    foptim::fmir::GreedyMatcher matcher;
    callee_mir = matcher.apply(*callee.func, conf);
    caller_mir = matcher.apply(*caller.func, conf);
    foptim::fmir::CallingConvImpl cc;
    cc.first_stage(callee_mir, foptim::fmir::CCallHelper, conf);
    cc.first_stage(caller_mir, foptim::fmir::CCallHelper, conf);
  }

  // matcher + the whole MIR pipeline (register allocation included)
  void run_mir_pipeline(const foptim::conf::CompConf &conf) {
    foptim::JobSheduler shed;
    shed.init(0);
    foptim::FVec<foptim::fmir::MFunc> funcs(2);
    foptim::TVec<foptim::fir::Function *> order{callee_f, caller_f};
    foptim::FVec<foptim::fmir::Global> globals;
    foptim::fmir::pipeline::optimize_mir(funcs, order, globals, &shed, conf);
    shed.deinit();
  }

  // registers of the ret_setups behind the invoke of the caller
  std::vector<foptim::fmir::CReg> call_result_regs() const {
    std::vector<foptim::fmir::CReg> regs;
    for (const auto &instr : caller_mir.bbs[0].instrs) {
      if (instr.is(foptim::fmir::GBaseSubtype::ret_setup)) {
        EXPECT_EQ(instr.n_args, 2);
        regs.push_back(instr.args[1].reg.c_reg());
      }
    }
    return regs;
  }

  // registers the callee moves its values into and returns
  std::vector<foptim::fmir::CReg> return_regs() const {
    const auto &ret = callee_mir.bbs[0].instrs.back();
    EXPECT_TRUE(ret.is(foptim::fmir::GBaseSubtype::ret));
    std::vector<foptim::fmir::CReg> regs;
    for (u32 i = 0; i < ret.n_args; i++) {
      regs.push_back(ret.args[i].reg.c_reg());
    }
    return regs;
  }
};
}  // namespace

TEST(MultiResultBackend, ReturnRegisters) {
  using foptim::fmir::CReg;
  using Regs = std::vector<CReg>;
  {
    MixedModule m{"ii"};
    EXPECT_EQ(m.return_regs(), (Regs{CReg::A, CReg::D}));
    EXPECT_EQ(m.call_result_regs(), (Regs{CReg::A, CReg::D}));
  }
  {
    MixedModule m{"dd"};
    EXPECT_EQ(m.return_regs(), (Regs{CReg::mm0, CReg::mm1}));
    EXPECT_EQ(m.call_result_regs(), (Regs{CReg::mm0, CReg::mm1}));
  }
  {
    MixedModule m{"id"};
    EXPECT_EQ(m.return_regs(), (Regs{CReg::A, CReg::mm0}));
    EXPECT_EQ(m.call_result_regs(), (Regs{CReg::A, CReg::mm0}));
  }
  {
    MixedModule m{"di"};
    EXPECT_EQ(m.return_regs(), (Regs{CReg::mm0, CReg::A}));
    EXPECT_EQ(m.call_result_regs(), (Regs{CReg::mm0, CReg::A}));
  }
  {
    MixedModule m{"idid"};
    EXPECT_EQ(m.return_regs(), (Regs{CReg::A, CReg::mm0, CReg::D, CReg::mm1}));
    EXPECT_EQ(m.call_result_regs(),
              (Regs{CReg::A, CReg::mm0, CReg::D, CReg::mm1}));
  }
}

TEST(MultiResultBackend, FullMirPipeline) {
  foptim::conf::CompConf conf;
  ASSERT_TRUE(conf.parse("Default"));
  for (const char *kinds : {"ii", "dd", "id", "di", "idid", "didi", "iidd"}) {
    MixedModule m{kinds};
    m.run_mir_pipeline(conf);
  }
}
