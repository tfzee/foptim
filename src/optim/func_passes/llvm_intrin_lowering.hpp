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

  void apply(fir::BasicBlock bb, fir::Function &func) {
    TVec<fir::Instr> instrs = {bb->instructions.begin(),
                               bb->instructions.end()};
    for (auto instr : instrs) {
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
        } else if (callee.func->name.starts_with("llvm.is.fpclass")) {
          handle_is_fpclass(instr, func, callee);
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
