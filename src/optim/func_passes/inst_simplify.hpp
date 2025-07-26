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
  case fir::BinaryInstrSubType::Or:
    if constexpr (std::is_integral_v<T>) {
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a | b, type)));
      return true;
    }
    TODO("impl");
  case fir::BinaryInstrSubType::And:
    if constexpr (std::is_integral_v<T>) {
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a & b, type)));
      return true;
    }
    TODO("impl");
  case fir::BinaryInstrSubType::Xor:
    if constexpr (std::is_integral_v<T>) {
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a ^ b, type)));
      return true;
    }
    TODO("impl");
  case fir::BinaryInstrSubType::IntSRem:
    if constexpr (std::is_integral_v<T>) {
      auto width = type->get_bitwidth();
      auto invwidth = (128 + 1 - width);
      auto extended_a = ((a << invwidth) >> invwidth);
      auto extended_b = ((b << invwidth) >> invwidth);
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(extended_a % extended_b, type)));
      return true;
    }
  case fir::BinaryInstrSubType::Shl:
    if constexpr (std::is_integral_v<T>) {
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a << b, type)));
      return true;
    }
  case fir::BinaryInstrSubType::IntURem:
  case fir::BinaryInstrSubType::AShr:
  case fir::BinaryInstrSubType::Shr:
    TODO("impl");
  case fir::BinaryInstrSubType::INVALID:
    return false;
  }
}

} // namespace foptim::optim
