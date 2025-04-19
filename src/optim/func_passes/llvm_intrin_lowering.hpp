#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

class LLVMInstrinsicLowering final : public FunctionPass {
public:
  void handle_memset(fir::Instr instr, fir::Function &func,
                     fir::FunctionR /*callee*/) {

    auto *ctx = func.ctx;
    fir::Builder bb{instr};
    auto target_ptr = instr->args[1];
    auto value = instr->args[2];
    auto size = instr->args[3];

    auto void_ty = ctx->get_void_type();
    auto *func_ty = instr->get_attrib("callee_type").try_type();
    assert(func_ty);

    fir::ValueR args[3] = {target_ptr, value, size};
    bb.build_call(fir::ValueR(ctx->get_constant_value(
                      ctx->get_function("foptim.memset"))),
                  *func_ty, void_ty, args);

    instr.destroy();
    // if (name == "llvm.memset.p0.i64") {

    // } else {
    //   TODO("impl more memset intrinsics");
    // }
  }

  void handle_is_fpclass(fir::Instr instr, fir::Function & /*func*/,
                         fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto val = instr->args[1];
    auto mode_a = instr->args[2];
    ASSERT(mode_a.is_constant());
    auto mode_c = mode_a.as_constant();
    ASSERT(mode_c->is_int());
    auto mode = mode_c->as_int();

    if (mode == 3) {
      auto result = bb.build_float_cmp(val, val, fir::FCmpInstrSubType::IsNaN);
      instr->replace_all_uses(result);
      instr.destroy();
    } else {
      // print << instr << "\n";
      TODO("impl");
    }
  }

  void handle_fmuladd(fir::Instr instr, fir::Function & /*func*/,
                      fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto mul_1 = instr->args[1];
    auto mul_2 = instr->args[2];
    auto add = instr->args[3];

    auto mul_res = bb.build_float_mul(mul_1, mul_2);
    auto result = bb.build_float_add(mul_res, add);
    instr->replace_all_uses(result);
    instr.destroy();
  }

  void handle_memcpy(fir::Instr instr, fir::Function &func,
                     fir::FunctionR /*callee*/) {

    auto *ctx = func.ctx;
    fir::Builder bb{instr};
    auto dst_ptr = instr->args[1];
    auto src_ptr = instr->args[2];
    auto size = instr->args[3];

    auto void_ty = ctx->get_void_type();
    auto *func_ty = instr->get_attrib("callee_type").try_type();
    assert(func_ty);

    fir::ValueR args[3] = {dst_ptr, src_ptr, size};

    bb.build_call(fir::ValueR(ctx->get_constant_value(
                      ctx->get_function("foptim.memcpy"))),
                  *func_ty, void_ty, args);

    instr.destroy();
    // if (name == "llvm.memset.p0.i64") {

    // } else {
    //   TODO("impl more memset intrinsics");
    // }
  }

  void handle_trap(fir::Instr instr, fir::Function &funcy,
                   fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto *ctx = funcy.ctx;
    auto abort_func = ctx->get_function("abort");
    auto void_type = ctx->get_void_type();

    bb.build_call(fir::ValueR{ctx->get_constant_value(abort_func)},
                  abort_func->func_ty, void_type, {});
    instr.destroy();
  }

  void handle_lifetime(fir::Instr instr, fir::Function & /*func*/,
                       fir::FunctionR /*callee*/) {
    instr.destroy();
  }

  void handle_fabs(fir::Instr instr, fir::Function &funcy,
                   fir::FunctionR /*callee*/) {
    auto width = instr.get_type()->as_float();
    fir::Builder bb{instr};
    auto *ctx = funcy.ctx;
    const auto *func_name = "INVALID_FUNC_NAME";

    if (width == 64) {
      func_name = "foptim.abs.f64";
    } else if (width == 32) {
      func_name = "foptim.abs.f32";
    } else {
      fmt::println("{}", instr);
      TODO("IMPL");
    }

    auto func = ctx->get_function(func_name);
    auto ret_type = instr.get_type();
    foptim::fir::ValueR args[1] = {instr->args[1]};
    auto res = bb.build_call(fir::ValueR{ctx->get_constant_value(func)},
                             func->func_ty, ret_type, args);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_abs(fir::Instr instr, fir::Function &funcy,
                  fir::FunctionR /*callee*/) {
    auto width = instr.get_type()->as_int();
    fir::Builder bb{instr};
    auto *ctx = funcy.ctx;
    const auto *func_name = "INVALID_FUNC_NAME";

    if (width == 64) {
      func_name = "foptim.abs.i64";
    } else if (width == 32) {
      func_name = "foptim.abs.i32";
    } else {
      fmt::println("{}", instr);
      TODO("IMPL");
    }

    auto func = ctx->get_function(func_name);
    auto ret_type = instr.get_type();
    foptim::fir::ValueR args[1] = {instr->args[1]};
    auto res = bb.build_call(fir::ValueR{ctx->get_constant_value(func)},
                             func->func_ty, ret_type, args);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_umul_with_overflow(fir::Instr instr, fir::Function & /*funcy*/,
                                 fir::FunctionR /*callee*/) {

    auto width = instr.get_type()->as_struct().elems[0].ty->as_int();
    if (width == 64) {
      fir::Builder bb{instr};

      auto mul_result = bb.build_int_mul(instr->args[1], instr->args[2]);
      auto overflow_result = bb.build_int_cmp(
          instr->args[1], instr->args[2], fir::ICmpInstrSubType::MulOverflow);

      // annoying copy
      auto uses = instr->uses;
      for (auto use : uses) {
        ASSERT(use.user->is(fir::InstrType::ExtractValue));
        ASSERT(use.user->args.size() == 2);
        ASSERT(use.argId == 0);
        auto index = use.user->args[1].as_constant()->as_int();
        if (index == 0) {
          use.user->replace_all_uses(mul_result);
          use.user.destroy();
        } else if (index == 1) {
          use.user->replace_all_uses(overflow_result);
          use.user.destroy();
        } else {
          UNREACH();
        }
      }
      instr.destroy();
    } else {
      TODO("IMPL");
    }
  }
  void handle_expect(fir::Instr instr, fir::Function & /*funcy*/,
                     fir::FunctionR /*callee*/) {
    instr->replace_all_uses(instr->args[1]);
    instr.destroy();
  }

  void handle_obj_size(fir::Instr instr, fir::Function &funcy,
                       fir::FunctionR /*callee*/) {
    auto *ctx = funcy.ctx;
    auto ret_type = instr->args[2].as_constant()->as_int();
    instr->replace_all_uses(fir::ValueR{
        ctx->get_constant_value(ret_type == 0 ? -1 : 0, instr->get_type())});
    instr.destroy();
  }

  void handle_va(fir::Instr instr, fir::Function &funcy,
                 fir::FunctionR /*callee*/, bool is_start) {
    auto *ctx = funcy.ctx;
    auto func =
        ctx->get_function(is_start ? "foptim.va_start" : "foptim.va_end");
    foptim::fir::ValueR args[1] = {instr->args[1]};
    fir::Builder bb{instr};
    auto res = bb.build_call(fir::ValueR{ctx->get_constant_value(func)},
                             func->func_ty, ctx->get_void_type(), args);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void apply(fir::BasicBlock bb, fir::Function &func) {
    // annoying copy
    //
    // TVec<fir::Instr> instrs = {bb->instructions.begin(),
    //                            bb->instructions.end()};
    for (size_t ip1 = bb->instructions.size(); ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      auto instr = bb->instructions[i];
      if (!instr.is_valid()) {
        // instruction might have been deleted in a prior iteration
        continue;
      }
      if (instr->is(fir::InstrType::CallInstr)) {
        // const auto *callee = instr->get_attrib("callee").try_string();
        if (!instr->args[0].is_constant()) {
          continue;
        }
        auto callee = instr->args[0].as_constant()->as_func();
        if (callee.func->name.starts_with("llvm.memset.")) {
          handle_memset(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.memcpy.")) {
          handle_memcpy(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.fmuladd.")) {
          handle_fmuladd(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.trap")) {
          handle_trap(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.fabs")) {
          handle_fabs(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.abs")) {
          handle_abs(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.is.fpclass")) {
          handle_is_fpclass(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.expect")) {
          handle_expect(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.objectsize")) {
          handle_obj_size(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.va_start")) {
          handle_va(instr, func, callee, true);
        } else if (callee.func->name.starts_with("llvm.va_end")) {
          handle_va(instr, func, callee, false);
        } else if (callee.func->name.starts_with("llvm.umul.with.overflow")) {
          handle_umul_with_overflow(instr, func, callee);
        } else if (callee.func->name.starts_with("llvm.lifetime.start") ||
                   callee.func->name.starts_with("llvm.lifetime.end")) {
          handle_lifetime(instr, func, callee);
        }
      }
    }
  }

  void apply(fir::Context & /*unused*/, fir::Function &func) override {
    ZoneScopedN("LLVMInstrinsLowering");
    for (auto bb : func.basic_blocks) {
      apply(bb, func);
    }
  }
};
} // namespace foptim::optim
