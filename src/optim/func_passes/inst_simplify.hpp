#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"

namespace foptim::optim {

class InstSimplify final : public FunctionPass {
public:
  struct WorkItem {
    fir::Instr instr;
    fir::BasicBlock b;
  };

  void apply(fir::Context &ctx, fir::Function &func) override;
};

template <class T>
bool try_constant_eval_binary(fir::Instr instr,
                              fir::BinaryInstrSubType sub_type, T a, T b,
                              fir::TypeR type, fir::Context &ctx) {
  switch (sub_type) {
  case fir::BinaryInstrSubType::FloatAdd:
  case fir::BinaryInstrSubType::IntAdd:
    instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(a + b, type)));
    return true;
  case fir::BinaryInstrSubType::IntMul:
  case fir::BinaryInstrSubType::FloatMul:
    instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(a * b, type)));
    return true;
  case fir::BinaryInstrSubType::FloatSub:
  case fir::BinaryInstrSubType::IntSub:
    instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(a - b, type)));
    return true;
  case fir::BinaryInstrSubType::FloatDiv:
    instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(a / b, type)));
    return true;
  case fir::BinaryInstrSubType::IntSDiv:
    instr->replace_all_uses(
        fir::ValueR(ctx->get_constant_value((i64)a / (i64)b, type)));
    return true;
  case fir::BinaryInstrSubType::IntUDiv:
    instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(a / b, type)));
    return true;
  case fir::BinaryInstrSubType::IntSRem:
  case fir::BinaryInstrSubType::Shl:
  case fir::BinaryInstrSubType::And:
  case fir::BinaryInstrSubType::Or:
  case fir::BinaryInstrSubType::Xor:
  case fir::BinaryInstrSubType::INVALID:
  case fir::BinaryInstrSubType::Shr:
  case fir::BinaryInstrSubType::AShr:
    return false;
  }
}

} // namespace foptim::optim
