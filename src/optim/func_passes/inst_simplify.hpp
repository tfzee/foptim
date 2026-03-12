#pragma once
#include <type_traits>

#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"

namespace foptim::optim {

constexpr bool TRACY_DEBUG_INST_SIMPLIFY = false;

class InstSimplify final : public FunctionPass {
 public:
  struct WorkItem {
    fir::Instr instr;
    fir::BasicBlock b;
  };

  void apply(fir::Context &ctx, fir::Function &func) override;
};

namespace InstSimp {

using WorkList = TVec<InstSimplify::WorkItem>;

inline void push_all_uses(WorkList &worklist, fir::Instr instr) {
  for (auto &use : instr->uses) {
    worklist.emplace_back(use.user, use.user->parent);
  }
}

template <class T>
bool try_constant_eval_binary(fir::Instr instr,
                              fir::BinaryInstrSubType sub_type, T a, T b,
                              fir::TypeR type, fir::Context &ctx) {
  switch (sub_type) {
    case fir::BinaryInstrSubType::FloatAdd:
    case fir::BinaryInstrSubType::IntAdd:
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a + b, type)));
      return true;
    case fir::BinaryInstrSubType::IntMul:
    case fir::BinaryInstrSubType::FloatMul:
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a * b, type)));
      return true;
    case fir::BinaryInstrSubType::FloatSub:
    case fir::BinaryInstrSubType::IntSub:
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a - b, type)));
      return true;
    case fir::BinaryInstrSubType::FloatDiv:
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a / b, type)));
      return true;
    case fir::BinaryInstrSubType::IntSDiv:
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value((i64)a / (i64)b, type)));
      return true;
    case fir::BinaryInstrSubType::IntUDiv:
      instr->replace_all_uses(
          fir::ValueR(ctx->get_constant_value(a / b, type)));
      return true;
    case fir::BinaryInstrSubType::Or:
      if constexpr (std::is_integral_v<T>) {
        instr->replace_all_uses(
            fir::ValueR(ctx->get_constant_value(a | b, type)));
        return true;
      } else if constexpr (std::is_same_v<T, float>) {
        instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(
            std::bit_cast<f32>(std::bit_cast<u32>(a) | std::bit_cast<u32>(b)),
            type)));
        return true;
      } else if constexpr (std::is_same_v<T, double>) {
        instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(
            std::bit_cast<f64>(std::bit_cast<u64>(a) | std::bit_cast<u64>(b)),
            type)));
        return true;
      }
      TODO("impl");
    case fir::BinaryInstrSubType::And:
      if constexpr (std::is_integral_v<T>) {
        instr->replace_all_uses(
            fir::ValueR(ctx->get_constant_value(a & b, type)));
        return true;
      } else if constexpr (std::is_same_v<T, float>) {
        instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(
            std::bit_cast<f32>(std::bit_cast<u32>(a) & std::bit_cast<u32>(b)),
            type)));
        return true;
      } else if constexpr (std::is_same_v<T, double>) {
        instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(
            std::bit_cast<f64>(std::bit_cast<u64>(a) & std::bit_cast<u64>(b)),
            type)));
        return true;
      }
      TODO("impl");
    case fir::BinaryInstrSubType::Xor:
      if constexpr (std::is_integral_v<T>) {
        instr->replace_all_uses(
            fir::ValueR(ctx->get_constant_value(a ^ b, type)));
        return true;
      } else if constexpr (std::is_same_v<T, float>) {
        instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(
            std::bit_cast<f32>(std::bit_cast<u32>(a) ^ std::bit_cast<u32>(b)),
            type)));
        return true;
      } else if constexpr (std::is_same_v<T, double>) {
        instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(
            std::bit_cast<f64>(std::bit_cast<u64>(a) ^ std::bit_cast<u64>(b)),
            type)));
        return true;
      }
      TODO("impl");
    case fir::BinaryInstrSubType::IntSRem:
      if constexpr (std::is_integral_v<T>) {
        auto width = type->get_bitwidth();
        auto invwidth = (128 + 1 - width);
        auto extended_a = (((i128)a << invwidth) >> invwidth);
        auto extended_b = (((i128)b << invwidth) >> invwidth);
        instr->replace_all_uses(fir::ValueR(
            ctx->get_constant_value(extended_a % extended_b, type)));
        return true;
      }
      TODO("impl");
    case fir::BinaryInstrSubType::IntURem:
      if constexpr (std::is_integral_v<T>) {
        instr->replace_all_uses(fir::ValueR(
            ctx->get_constant_value((i128)((u128)a % (u128)b), type)));
        return true;
      }
      TODO("impl");
    case fir::BinaryInstrSubType::Shl:
      if constexpr (std::is_integral_v<T>) {
        instr->replace_all_uses(
            fir::ValueR(ctx->get_constant_value(a << b, type)));
        return true;
      }
      TODO("impl");
    case fir::BinaryInstrSubType::Shr:
      if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
        instr->replace_all_uses(
            fir::ValueR(ctx->get_constant_value(a >> b, type)));
        return true;
      } else if constexpr (std::is_integral_v<T>) {
        using unsigned_ty = std::make_unsigned_t<T>;
        instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(
            std::bit_cast<T>(std::bit_cast<unsigned_ty>(a)) >>
                std::bit_cast<T>(std::bit_cast<unsigned_ty>(b)),
            type)));
        return true;
      }
      TODO("impl");
    case fir::BinaryInstrSubType::AShr:
      if constexpr (std::is_integral_v<T>) {
        auto width = type->get_bitwidth();
        auto invwidth = (128 + 1 - width);
        auto extended_a = ((a << invwidth) >> invwidth);
        instr->replace_all_uses(
            fir::ValueR(ctx->get_constant_value(extended_a >> b, type)));
        return true;
      }
      TODO("impl");
    case fir::BinaryInstrSubType::INVALID:
      return false;
  }
}

}  // namespace InstSimp
}  // namespace foptim::optim
