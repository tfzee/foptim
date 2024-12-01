
#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

class LLVMInstrinsicLowering final : public FunctionPass {
public:
  void handle_memset(fir::Instr instr, fir::Function &func,
                     const std::string & /*unused*/) {

    auto *ctx = func.ctx;
    fir::Builder bb{instr};
    auto target_ptr = instr->args[0];
    auto value = instr->args[1];
    auto size = instr->args[2];

    auto void_ty = ctx->get_void_type();
    auto *func_ty = instr->get_attrib("callee_type").try_type();
    assert(func_ty);

    fir::ValueR args[3] = {target_ptr, value, size};
    bb.build_direct_call("foptim.memset", *func_ty, void_ty, args);

    instr.remove_from_parent();
    // if (name == "llvm.memset.p0.i64") {

    // } else {
    //   TODO("impl more memset intrinsics");
    // }
  }

  void handle_memcpy(fir::Instr instr, fir::Function &func,
                     const std::string & /*unused*/) {

    auto *ctx = func.ctx;
    fir::Builder bb{instr};
    auto dst_ptr = instr->args[0];
    auto src_ptr = instr->args[1];
    auto size = instr->args[2];

    auto void_ty = ctx->get_void_type();
    auto *func_ty = instr->get_attrib("callee_type").try_type();
    assert(func_ty);

    fir::ValueR args[3] = {dst_ptr, src_ptr, size};
    bb.build_direct_call("foptim.memcpy", *func_ty, void_ty, args);

    instr.remove_from_parent();
    // if (name == "llvm.memset.p0.i64") {

    // } else {
    //   TODO("impl more memset intrinsics");
    // }
  }

  void apply(fir::BasicBlock bb, fir::Function &func) {
    for (auto instr : bb->instructions) {
      if (instr->is(fir::InstrType::DirectCallInstr)) {
        const auto *callee = instr->get_attrib("callee").try_string();
        if (callee->starts_with("llvm.memset.")) {
          handle_memset(instr, func, *callee);
        } else if (callee->starts_with("llvm.memcpy.")) {
          handle_memcpy(instr, func, *callee);
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
