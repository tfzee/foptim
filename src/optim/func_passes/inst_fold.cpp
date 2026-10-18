#include "inst_fold.hpp"

#include <cmath>

#include "ir/basic_block.hpp"
#include "ir/constant_value.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/attributer/KnownBits.hpp"
#include "optim/func_passes/inst_simplify/simplify_fcmp.hpp"
#include "utils/todo.hpp"
#include "utils/tracy.hpp"

namespace foptim::optim {
namespace InstFold {
namespace {

using Res = std::optional<fir::ValueR>;
using BinSub = fir::BinaryInstrSubType;
using ICmpSub = fir::ICmpInstrSubType;

fir::ValueR val(fir::ConstantValueR c) { return fir::ValueR{c}; }

Res poison_of(fir::Instr instr, fir::Context &ctx) {
  return val(ctx->get_poisson_value(instr->result_type()));
}

// a constant of the type of the instruction, only for plain integers
Res int_result(fir::Instr instr, fir::Context &ctx, APInt v) {
  return val(ctx->intern_constant(
      fir::ConstantValue(std::move(v), instr->result_type())));
}

// the result of a compare, i1 (or any integer) typed
Res bool_result(fir::Instr instr, fir::Context &ctx, bool b) {
  if (!instr->result_type()->is_int()) {
    return std::nullopt;
  }
  return int_result(instr, ctx,
                    APInt(instr->result_type()->get_bitwidth(), u64{b}));
}

// null pointers are the zero integer
APInt int_operand(const fir::ConstantValue &c, u32 bits) {
  return c.is_null() ? APInt::zero(bits) : c.as_apint_resized(bits);
}

// operations that commute with truncating, so a splat constant of a vector
// can be computed on the whole width
bool lane_safe(BinSub sub) {
  return sub == BinSub::IntAdd || sub == BinSub::IntSub ||
         sub == BinSub::IntMul || sub == BinSub::And || sub == BinSub::Or ||
         sub == BinSub::Xor;
}

Res fold_int_binary(fir::Instr instr, fir::Context &ctx, BinSub sub,
                    const fir::ConstantValue &ca,
                    const fir::ConstantValue &cb) {
  const auto type = instr->result_type();
  if (type->is_vec() ? !lane_safe(sub) : !type->is_int_or_ptr()) {
    return std::nullopt;
  }
  const u32 bw = type->get_bitwidth();
  const APInt a = int_operand(ca, bw);
  const APInt b = int_operand(cb, bw);
  auto poison = [&] { return poison_of(instr, ctx); };
  auto result = [&](APInt v) { return int_result(instr, ctx, std::move(v)); };
  const bool shift_ok = b.ult(APInt(bw, u64{bw}));
  const u32 amt = shift_ok ? static_cast<u32>(b.to_u64()) : 0;
  switch (sub) {
    case BinSub::IntAdd:
    case BinSub::PtrAdd:
      return result(a + b);
    case BinSub::IntMul:
      return result(a * b);
    case BinSub::IntSub:
      return result(a - b);
    case BinSub::IntSDiv:
      if (b.is_zero() || (a == APInt::signed_min(bw) && b.is_all_ones())) {
        return poison();
      }
      return result(a.sdiv(b));
    case BinSub::IntSRem:
      if (b.is_zero() || (a == APInt::signed_min(bw) && b.is_all_ones())) {
        return poison();
      }
      return result(a.srem(b));
    case BinSub::IntUDiv:
      return b.is_zero() ? poison() : result(a.udiv(b));
    case BinSub::IntURem:
      return b.is_zero() ? poison() : result(a.urem(b));
    case BinSub::Or:
      return result(a | b);
    case BinSub::And:
      return result(a & b);
    case BinSub::Xor:
      return result(a ^ b);
    case BinSub::Shl:
      return shift_ok ? result(a.shl(amt)) : poison();
    case BinSub::Shr:
      return shift_ok ? result(a.lshr(amt)) : poison();
    case BinSub::AShr:
      return shift_ok ? result(a.ashr(amt)) : poison();
    case BinSub::FloatAdd:
    case BinSub::FloatMul:
    case BinSub::FloatSub:
    case BinSub::FloatDiv:
    case BinSub::INVALID:
      return std::nullopt;
  }
  return std::nullopt;
}

// T is f32 or f64
template <class T>
Res fold_float_binary(fir::Instr instr, fir::Context &ctx, BinSub sub, T a,
                      T b) {
  using Bits = std::conditional_t<std::is_same_v<T, f32>, u32, u64>;
  const auto type = instr->result_type();
  auto result = [&](T v) { return val(ctx->get_constant_value(v, type)); };
  switch (sub) {
    case BinSub::FloatAdd:
      return result(a + b);
    case BinSub::FloatMul:
      return result(a * b);
    case BinSub::FloatSub:
      return result(a - b);
    case BinSub::FloatDiv:
      if (b == 0) {
        return poison_of(instr, ctx);
      }
      return result(a / b);
    case BinSub::Or:
      return result(std::bit_cast<T>(
          static_cast<Bits>(std::bit_cast<Bits>(a) | std::bit_cast<Bits>(b))));
    case BinSub::And:
      return result(std::bit_cast<T>(
          static_cast<Bits>(std::bit_cast<Bits>(a) & std::bit_cast<Bits>(b))));
    case BinSub::Xor:
      return result(std::bit_cast<T>(
          static_cast<Bits>(std::bit_cast<Bits>(a) ^ std::bit_cast<Bits>(b))));
    default:
      return std::nullopt;
  }
}

// operations with one constant operand that give a constant or the other
// operand
Res fold_int_identity(fir::Instr instr, fir::Context &ctx, BinSub sub) {
  const auto type = instr->result_type();
  if (!type->is_int_or_ptr() && !type->is_vec()) {
    return std::nullopt;
  }
  const u32 bw = type->get_bitwidth();
  fir::ValueR lhs = instr->args[0];
  fir::ValueR rhs = instr->args[1];
  auto is_int_const = [](fir::ValueR v) {
    return v.is_constant() && v.as_constant()->is_int();
  };
  // c is the constant operand, x the other one
  fir::ValueR x = lhs;
  fir::ValueR c = rhs;
  bool c_is_rhs = true;
  if (!is_int_const(rhs)) {
    if (!is_int_const(lhs) || !instr->is_commutative()) {
      return std::nullopt;
    }
    x = rhs;
    c = lhs;
    c_is_rhs = false;
  }
  const APInt cv = c.as_constant()->as_apint_resized(bw);
  auto zero = [&] { return int_result(instr, ctx, APInt::zero(bw)); };
  switch (sub) {
    case BinSub::IntAdd:
      if (cv.is_zero()) {
        return x;
      }
      break;
    case BinSub::PtrAdd:
    case BinSub::IntSub:
      if (c_is_rhs && cv.is_zero()) {
        return x;
      }
      break;
    case BinSub::IntMul:
      if (cv.is_one()) {
        return x;
      }
      if (cv.is_zero()) {
        return zero();
      }
      break;
    case BinSub::And:
      if (cv.is_all_ones()) {
        return x;
      }
      if (cv.is_zero()) {
        return zero();
      }
      break;
    case BinSub::Or:
      if (cv.is_zero()) {
        return x;
      }
      if (cv.is_all_ones()) {
        return int_result(instr, ctx, APInt::all_ones(bw));
      }
      break;
    case BinSub::Xor:
      if (cv.is_zero()) {
        return x;
      }
      break;
    case BinSub::IntSDiv:
    case BinSub::IntUDiv:
      if (c_is_rhs && cv.is_one()) {
        return x;
      }
      break;
    case BinSub::IntSRem:
    case BinSub::IntURem:
      if (c_is_rhs && cv.is_one()) {
        return zero();
      }
      break;
    case BinSub::Shl:
    case BinSub::Shr:
    case BinSub::AShr: {
      if (!c_is_rhs || !type->is_int()) {
        break;
      }
      const APInt &amt = c.as_constant()->as_apint();
      if (!amt.fits_u64() || amt.to_u64() >= bw) {
        return poison_of(instr, ctx);
      }
      if (amt.is_zero()) {
        return x;
      }
      break;
    }
    default:
      break;
  }
  return std::nullopt;
}

Res fold_binary(fir::Instr instr, fir::Context &ctx) {
  const auto sub = static_cast<BinSub>(instr->subtype);
  const fir::ValueR a = instr->args[0];
  const fir::ValueR b = instr->args[1];
  if (a.is_poison() || b.is_poison()) {
    return poison_of(instr, ctx);
  }
  if (a.is_constant() && b.is_constant()) {
    const auto ca = a.as_constant();
    const auto cb = b.as_constant();
    if ((ca->is_int() || ca->is_null()) && (cb->is_int() || cb->is_null())) {
      if (auto r = fold_int_binary(instr, ctx, sub, *ca.get_raw_ptr(),
                                   *cb.get_raw_ptr())) {
        return r;
      }
    } else if (cb->type->is_float() && cb->type->as_float() == 32 &&
               ca->type->is_float() && ca->type->as_float() == 32) {
      return fold_float_binary<f32>(instr, ctx, sub, ca->as_f32(),
                                    cb->as_f32());
    } else if (cb->type->is_float() && cb->type->as_float() == 64 &&
               ca->type->is_float() && ca->type->as_float() == 64) {
      return fold_float_binary<f64>(instr, ctx, sub, ca->as_f64(),
                                    cb->as_f64());
    }
  }
  // an inbounds add on a null base is undefined
  if ((sub == BinSub::PtrAdd || sub == BinSub::IntAdd) && instr->InBounds &&
      a.is_constant_null()) {
    return poison_of(instr, ctx);
  }
  // x & x, x | x
  if (!a.is_constant() && a == b && (sub == BinSub::And || sub == BinSub::Or)) {
    return a;
  }
  // x - x, x ^ x
  if (!a.is_constant() && a == b && instr->result_type()->is_int() &&
      (sub == BinSub::IntSub || sub == BinSub::Xor)) {
    return int_result(instr, ctx,
                      APInt::zero(instr->result_type()->get_bitwidth()));
  }
  return fold_int_identity(instr, ctx, sub);
}

Res fold_unary(fir::Instr instr, fir::Context &ctx) {
  const fir::ValueR a = instr->args[0];
  if (a.is_poison()) {
    return poison_of(instr, ctx);
  }
  const auto sub = static_cast<fir::UnaryInstrSubType>(instr->subtype);
  if (sub == fir::UnaryInstrSubType::Not && a.is_instr() &&
      a.as_instr()->is(fir::UnaryInstrSubType::Not)) {
    return a.as_instr()->args[0];
  }
  if (!a.is_constant()) {
    return std::nullopt;
  }
  const auto c = a.as_constant();
  const auto type = instr->result_type();
  if (sub == fir::UnaryInstrSubType::Not && type->is_int() && c->is_int()) {
    return int_result(instr, ctx, ~c->as_apint_resized(type->get_bitwidth()));
  }
  if (sub == fir::UnaryInstrSubType::IntNeg && type->is_int() && c->is_int()) {
    return int_result(instr, ctx, -c->as_apint_resized(type->get_bitwidth()));
  }
  if (sub == fir::UnaryInstrSubType::FloatNeg && type->is_float() &&
      c->is_float()) {
    if (type->as_float() == 32) {
      return val(ctx->get_constant_value(-c->as_f32(), type));
    }
    if (type->as_float() == 64) {
      return val(ctx->get_constant_value(-c->as_f64(), type));
    }
  }
  return std::nullopt;
}

Res fold_extend(fir::Instr instr, fir::Context &ctx) {
  const fir::ValueR a = instr->args[0];
  if (a.get_type() == instr->result_type()) {
    return a;
  }
  if (a.is_poison()) {
    return poison_of(instr, ctx);
  }
  if (a.is_constant() && a.as_constant()->is_int()) {
    const APInt &c = a.as_constant()->as_apint();
    const u32 out_width = instr->result_type()->get_bitwidth();
    return int_result(instr, ctx,
                      instr->is(fir::InstrType::ZExt)
                          ? c.zext_or_trunc(out_width)
                          : c.sext_or_trunc(out_width));
  }
  return std::nullopt;
}

Res fold_itrunc(fir::Instr instr, fir::Context &ctx) {
  const fir::ValueR a = instr->args[0];
  if (a.get_type() == instr->result_type()) {
    return a;
  }
  if (a.is_constant() && !a.is_constant_global() && !a.is_constant_func()) {
    const auto c = a.as_constant();
    if (c->is_poison()) {
      return poison_of(instr, ctx);
    }
    if (c->is_int() &&
        instr->result_type()->get_bitwidth() < a.get_type()->get_bitwidth()) {
      return val(ctx->get_constant_int(
          c->as_apint().trunc(instr->result_type()->get_bitwidth())));
    }
  }
  return std::nullopt;
}

Res fold_conversion(fir::Instr instr, fir::Context &ctx) {
  const fir::ValueR a = instr->args[0];
  const auto type = instr->result_type();
  if (instr->get_n_args() == 1 && a.is_poison()) {
    return poison_of(instr, ctx);
  }
  const auto sub = static_cast<fir::ConversionSubType>(instr->subtype);
  switch (sub) {
    case fir::ConversionSubType::INVALID:
      return std::nullopt;
    case fir::ConversionSubType::BitCast: {
      // int <-> ptr casts are rewritten by InstSimplify
      if ((a.get_type()->is_int() && type->is_ptr()) ||
          (type->is_int() && a.get_type()->is_ptr())) {
        return std::nullopt;
      }
      if (a.get_type() == type) {
        return a;
      }
      if (a.is_instr()) {
        auto a0 = a.as_instr();
        if (a0->is(fir::ConversionSubType::BitCast) &&
            a0->args[0].get_type() == type) {
          return a0->args[0];
        }
      }
      if (a.is_constant()) {
        if (auto r = a.as_constant()->bit_cast(ctx, type)) {
          return val(*r);
        }
      }
      return std::nullopt;
    }
    case fir::ConversionSubType::SITOFP:
    case fir::ConversionSubType::UITOFP: {
      if (!a.is_constant() || !a.as_constant()->is_int()) {
        return std::nullopt;
      }
      const APInt &v = a.as_constant()->as_apint();
      if (v.bits() > 128 || !type->is_float()) {
        return std::nullopt;
      }
      const bool is_signed = sub == fir::ConversionSubType::SITOFP;
      if (type->as_float() == 32) {
        const f32 f = is_signed ? static_cast<f32>(v.to_i128())
                                : static_cast<f32>(v.to_u128());
        return val(ctx->get_constant_value(f, type));
      }
      if (type->as_float() == 64) {
        const f64 f = is_signed ? static_cast<f64>(v.to_i128())
                                : static_cast<f64>(v.to_u128());
        return val(ctx->get_constant_value(f, type));
      }
      return std::nullopt;
    }
    case fir::ConversionSubType::PtrToInt:
      if (a.is_constant() || a.get_type()->is_int()) {
        return a;
      }
      if (a.is_instr() && a.as_instr()->is(fir::ConversionSubType::IntToPtr)) {
        return a.as_instr()->args[0];
      }
      return std::nullopt;
    case fir::ConversionSubType::IntToPtr:
      if (a.is_constant() || a.get_type()->is_ptr()) {
        return a;
      }
      if (a.is_instr() && a.as_instr()->is(fir::ConversionSubType::PtrToInt)) {
        return a.as_instr()->args[0];
      }
      return std::nullopt;
    case fir::ConversionSubType::FPTOSI: {
      if (!a.is_constant() || !a.as_constant()->is_float()) {
        return std::nullopt;
      }
      const auto c = a.as_constant();
      const i128 v = a.get_type()->get_bitwidth() == 32
                         ? static_cast<i128>(c->as_f32())
                         : static_cast<i128>(c->as_f64());
      return val(ctx->get_constant_value(v, type));
    }
    case fir::ConversionSubType::FPTOUI: {
      if (!a.is_constant() || !a.as_constant()->is_float()) {
        return std::nullopt;
      }
      const auto v = InstSimp::const_fp(a.as_constant());
      return val(ctx->get_constant_value(static_cast<u64>(v), type));
    }
    case fir::ConversionSubType::FPEXT: {
      if (!a.is_constant() || !a.as_constant()->is_float()) {
        return std::nullopt;
      }
      if (a.get_type() == type) {
        return val(ctx->get_constant_value(a.as_constant()->as_f64(), type));
      }
      if (a.get_type()->as_float() != 32 || type->as_float() != 64) {
        return std::nullopt;
      }
      return val(ctx->get_constant_value(
          static_cast<f64>(a.as_constant()->as_f32()), type));
    }
    case fir::ConversionSubType::FPTRUNC: {
      if (!a.is_constant() || !a.as_constant()->is_float()) {
        return std::nullopt;
      }
      if (a.get_type()->as_float() != 64 || type->as_float() != 32) {
        return std::nullopt;
      }
      return val(ctx->get_constant_value(
          static_cast<f32>(a.as_constant()->as_f64()), type));
    }
  }
  return std::nullopt;
}

Res fold_select(fir::Instr instr, fir::Context &ctx) {
  (void)ctx;
  const fir::ValueR cond = instr->args[0];
  if (cond.is_constant()) {
    const auto c = cond.as_constant();
    if (c->is_poison()) {
      return instr->args[2];
    }
    if (c->is_int()) {
      return c->as_apint().is_zero() ? instr->args[2] : instr->args[1];
    }
  }
  if (instr->args[1] == instr->args[2]) {
    return instr->args[1];
  }
  return std::nullopt;
}

Res icmp_known_bits(fir::Instr instr, fir::Context &ctx,
                    AttributerManager &man) {
  const auto sub_type = static_cast<ICmpSub>(instr->get_instr_subtype());
  const auto *bits1 = man.get_or_create_analysis<KnownBits>(instr->args[0]);
  const auto *bits2 = man.get_or_create_analysis<KnownBits>(instr->args[1]);
  man.run(ctx);
  if (bits1->width() != bits2->width()) {
    return std::nullopt;
  }
  bool evals_to_true = false;
  bool evals_to_false = false;
  switch (sub_type) {
    case ICmpSub::UGT:
      evals_to_true =
          bits1->get_unsigned_min_value().ugt(bits2->get_unsigned_max_value());
      evals_to_false =
          bits1->get_unsigned_max_value().ule(bits2->get_unsigned_min_value());
      break;
    case ICmpSub::UGE:
      evals_to_true =
          bits1->get_unsigned_min_value().uge(bits2->get_unsigned_max_value());
      evals_to_false =
          bits1->get_unsigned_max_value().ult(bits2->get_unsigned_min_value());
      break;
    case ICmpSub::ULT:
      evals_to_true =
          bits1->get_unsigned_max_value().ult(bits2->get_unsigned_min_value());
      evals_to_false =
          bits1->get_unsigned_min_value().uge(bits2->get_unsigned_max_value());
      break;
    case ICmpSub::ULE:
      evals_to_true =
          bits1->get_unsigned_max_value().ule(bits2->get_unsigned_min_value());
      evals_to_false =
          bits1->get_unsigned_min_value().ugt(bits2->get_unsigned_max_value());
      break;
    case ICmpSub::SGT:
      evals_to_true =
          bits1->get_signed_min_value().sgt(bits2->get_signed_max_value());
      evals_to_false =
          bits1->get_signed_max_value().sle(bits2->get_signed_min_value());
      break;
    case ICmpSub::SGE:
      evals_to_true =
          bits1->get_signed_min_value().sge(bits2->get_signed_max_value());
      evals_to_false =
          bits1->get_signed_max_value().slt(bits2->get_signed_min_value());
      break;
    case ICmpSub::SLT:
      evals_to_true =
          bits1->get_signed_max_value().slt(bits2->get_signed_min_value());
      evals_to_false =
          bits1->get_signed_min_value().sge(bits2->get_signed_max_value());
      break;
    case ICmpSub::SLE:
      evals_to_true =
          bits1->get_signed_max_value().sle(bits2->get_signed_min_value());
      evals_to_false =
          bits1->get_signed_min_value().sgt(bits2->get_signed_max_value());
      break;
    case ICmpSub::EQ:
    case ICmpSub::NE: {
      // a bit that is known one in one and known zero in the other
      const bool differ = !(bits1->known_one & bits2->known_zero).is_zero() ||
                          !(bits1->known_zero & bits2->known_one).is_zero();
      evals_to_false = sub_type == ICmpSub::EQ && differ;
      evals_to_true = sub_type == ICmpSub::NE && differ;
      break;
    }
    default:
      break;
  }
  ASSERT(!evals_to_false || !evals_to_true);
  if (evals_to_true || evals_to_false) {
    return bool_result(instr, ctx, evals_to_true);
  }
  return std::nullopt;
}

Res fold_icmp(fir::Instr instr, fir::Context &ctx, AttributerManager &man) {
  if (!instr->result_type()->is_int()) {
    return std::nullopt;
  }
  const auto sub = static_cast<ICmpSub>(instr->get_instr_subtype());
  const fir::ValueR a = instr->args[0];
  const fir::ValueR b = instr->args[1];
  const bool first_constant = a.is_constant();
  const bool second_constant = b.is_constant();

  // x cmp x
  if (!first_constant && a == b) {
    switch (sub) {
      case ICmpSub::EQ:
      case ICmpSub::ULE:
      case ICmpSub::UGE:
      case ICmpSub::SLE:
      case ICmpSub::SGE:
        return bool_result(instr, ctx, true);
      case ICmpSub::NE:
      case ICmpSub::ULT:
      case ICmpSub::UGT:
      case ICmpSub::SLT:
      case ICmpSub::SGT:
        return bool_result(instr, ctx, false);
      default:
        break;
    }
  }

  if (first_constant && second_constant) {
    const auto c1 = a.as_constant();
    const auto c2 = b.as_constant();
    if (c1->is_poison() || c2->is_poison()) {
      return poison_of(instr, ctx);
    }
    if (c1->is_global() && c2->is_global()) {
      const bool are_equal = c1->as_global() == c2->as_global();
      if (sub == ICmpSub::EQ) {
        return bool_result(instr, ctx, are_equal);
      }
      if (sub == ICmpSub::NE) {
        return bool_result(instr, ctx, !are_equal);
      }
    }
    if (sub == ICmpSub::EQ || sub == ICmpSub::NE) {
      const bool is_neq = sub == ICmpSub::NE;
      const bool first_non_null =
          c1->is_global() || (c1->is_func() && !c1->as_func()->is_decl());
      const bool second_non_null =
          c2->is_global() || (c2->is_func() && !c2->as_func()->is_decl());
      const bool first_null = c1->is_null();
      const bool second_null = c2->is_null();
      if ((first_non_null && second_null) || (first_null && second_non_null)) {
        return bool_result(instr, ctx, is_neq);
      }
      if (first_null && second_null) {
        return bool_result(instr, ctx, !is_neq);
      }
    }
    if (c1->is_global() || c1->is_func() || c2->is_global() || c2->is_func()) {
      return std::nullopt;
    }
    const u32 bitwidth =
        std::max(c1->type->get_bitwidth(), c2->type->get_bitwidth());
    auto operand = [&](const fir::ConstantValueR &c) -> std::optional<APInt> {
      if (c->is_int()) {
        return c->as_apint_resized(bitwidth);
      }
      if (c->is_null()) {
        return APInt::zero(bitwidth);
      }
      return std::nullopt;
    };
    const auto v1 = operand(c1);
    const auto v2 = operand(c2);
    if (!v1 || !v2) {
      return std::nullopt;
    }
    bool is_true = false;
    switch (sub) {
      case ICmpSub::SLT:
        is_true = v1->slt(*v2);
        break;
      case ICmpSub::ULT:
        is_true = v1->ult(*v2);
        break;
      case ICmpSub::NE:
        is_true = *v1 != *v2;
        break;
      case ICmpSub::EQ:
        is_true = *v1 == *v2;
        break;
      case ICmpSub::SGT:
        is_true = v1->sgt(*v2);
        break;
      case ICmpSub::UGT:
        is_true = v1->ugt(*v2);
        break;
      case ICmpSub::UGE:
        is_true = v1->uge(*v2);
        break;
      case ICmpSub::ULE:
        is_true = v1->ule(*v2);
        break;
      case ICmpSub::SGE:
        is_true = v1->sge(*v2);
        break;
      case ICmpSub::SLE:
        is_true = v1->sle(*v2);
        break;
      case ICmpSub::INVALID:
        return std::nullopt;
    }
    return bool_result(instr, ctx, is_true);
  }
  if (first_constant || second_constant) {
    // the analysis knows the constant side exactly
    if ((first_constant && a.as_constant()->is_poison()) ||
        (second_constant && b.as_constant()->is_poison())) {
      return poison_of(instr, ctx);
    }
  }
  return icmp_known_bits(instr, ctx, man);
}

Res fold_fcmp(fir::Instr instr, fir::Context &ctx) {
  if (!instr->result_type()->is_int()) {
    return std::nullopt;
  }
  const fir::ValueR a = instr->args[0];
  const fir::ValueR b = instr->args[1];
  if (!a.is_constant() || !b.is_constant()) {
    return std::nullopt;
  }
  const auto c1 = a.as_constant();
  const auto c2 = b.as_constant();
  if (c1->is_poison() || c2->is_poison()) {
    return poison_of(instr, ctx);
  }
  if (!c1->is_float() || !c2->is_float()) {
    return std::nullopt;
  }
  const auto v1 = InstSimp::const_fp(c1);
  const auto v2 = InstSimp::const_fp(c2);
  const bool uno = std::isnan(v1) || std::isnan(v2);
  bool is_true = false;
  switch (static_cast<fir::FCmpInstrSubType>(instr->get_instr_subtype())) {
    case fir::FCmpInstrSubType::IsNaN:
    case fir::FCmpInstrSubType::UNO:
      is_true = uno;
      break;
    case fir::FCmpInstrSubType::ORD:
      is_true = !uno;
      break;
    case fir::FCmpInstrSubType::OEQ:
      is_true = !uno && v1 == v2;
      break;
    case fir::FCmpInstrSubType::OGT:
      is_true = !uno && v1 > v2;
      break;
    case fir::FCmpInstrSubType::OGE:
      is_true = !uno && v1 >= v2;
      break;
    case fir::FCmpInstrSubType::OLT:
      is_true = !uno && v1 < v2;
      break;
    case fir::FCmpInstrSubType::OLE:
      is_true = !uno && v1 <= v2;
      break;
    case fir::FCmpInstrSubType::ONE:
      is_true = !uno && v1 != v2;
      break;
    case fir::FCmpInstrSubType::UEQ:
      is_true = uno || v1 == v2;
      break;
    case fir::FCmpInstrSubType::UGT:
      is_true = uno || v1 > v2;
      break;
    case fir::FCmpInstrSubType::UGE:
      is_true = uno || v1 >= v2;
      break;
    case fir::FCmpInstrSubType::ULT:
      is_true = uno || v1 < v2;
      break;
    case fir::FCmpInstrSubType::ULE:
      is_true = uno || v1 <= v2;
      break;
    case fir::FCmpInstrSubType::UNE:
      is_true = uno || v1 != v2;
      break;
    case fir::FCmpInstrSubType::AlwFalse:
      is_true = false;
      break;
    case fir::FCmpInstrSubType::AlwTrue:
      is_true = true;
      break;
    case fir::FCmpInstrSubType::INVALID:
      return std::nullopt;
  }
  return bool_result(instr, ctx, is_true);
}

Res fold_intrinsic(fir::Instr instr, fir::Context &ctx) {
  const auto sub = static_cast<fir::IntrinsicSubType>(instr->subtype);
  const auto type = instr->result_type();
  switch (sub) {
    case fir::IntrinsicSubType::Abs: {
      if (!instr->args[0].is_constant() ||
          !instr->args[0].as_constant()->is_int()) {
        return std::nullopt;
      }
      const APInt &v = instr->args[0].as_constant()->as_apint();
      return int_result(instr, ctx, v.is_neg() ? -v : v);
    }
    case fir::IntrinsicSubType::UMin:
    case fir::IntrinsicSubType::UMax:
    case fir::IntrinsicSubType::SMin:
    case fir::IntrinsicSubType::SMax: {
      if (!instr->args[0].is_constant() || !instr->args[1].is_constant() ||
          !instr->args[0].as_constant()->is_int() ||
          !instr->args[1].as_constant()->is_int()) {
        return std::nullopt;
      }
      const APInt &a = instr->args[0].as_constant()->as_apint();
      const APInt &b = instr->args[1].as_constant()->as_apint();
      if (a.bits() != b.bits()) {
        return std::nullopt;
      }
      bool take_b = false;
      switch (sub) {
        case fir::IntrinsicSubType::SMax:
          take_b = a.slt(b);
          break;
        case fir::IntrinsicSubType::SMin:
          take_b = b.slt(a);
          break;
        case fir::IntrinsicSubType::UMax:
          take_b = a.ult(b);
          break;
        default:
          take_b = b.ult(a);
          break;
      }
      return int_result(instr, ctx, take_b ? b : a);
    }
    case fir::IntrinsicSubType::FMin:
    case fir::IntrinsicSubType::FMax: {
      if (!instr->args[0].is_constant() || !instr->args[1].is_constant() ||
          !instr->args[0].as_constant()->is_float() ||
          !instr->args[1].as_constant()->is_float() || !type->is_float()) {
        return std::nullopt;
      }
      const auto x = InstSimp::const_fp(instr->args[0].as_constant());
      const auto y = InstSimp::const_fp(instr->args[1].as_constant());
      const auto v =
          sub == fir::IntrinsicSubType::FMax ? std::max(x, y) : std::min(x, y);
      return type->as_float() == 32
                 ? val(ctx->get_constant_value(static_cast<f32>(v), type))
                 : val(ctx->get_constant_value(v, type));
    }
    case fir::IntrinsicSubType::FAbs: {
      if (!instr->args[0].is_constant()) {
        return std::nullopt;
      }
      if (instr->args[0].is_poison()) {
        return poison_of(instr, ctx);
      }
      if (!type->is_float() || !instr->args[0].as_constant()->is_float()) {
        return std::nullopt;
      }
      if (type->as_float() == 32) {
        return val(ctx->get_constant_value(
            std::fabs(instr->args[0].as_constant()->as_f32()), type));
      }
      if (type->as_float() == 64) {
        return val(ctx->get_constant_value(
            std::fabs(instr->args[0].as_constant()->as_f64()), type));
      }
      return std::nullopt;
    }
    default:
      return std::nullopt;
  }
}

Res fold_global_load(fir::Instr instr, fir::Context &ctx) {
  const auto arg0_const = instr->args[0].as_constant();
  const auto glob = arg0_const->as_global();
  if (!glob->reloc_info.empty() || !glob->is_constant ||
      (glob->linkage != fir::Linkage::Internal &&
       glob->linkage != fir::Linkage::LinkOnceODR)) {
    return std::nullopt;
  }
  auto load_type = instr->result_type();
  const auto out_bitwidth = load_type->get_bitwidth();
  if (load_type->is_int()) {
    if (out_bitwidth > 64) {
      return std::nullopt;
    }
    // TODO: kinda iffy with the loading oversize - prob not a problem
    // since the pages prob >8 byte aligned anyway :)
    u64 v = 0;
    if (out_bitwidth <= 8) {
      v = *glob->init_value;
    } else if (out_bitwidth <= 16) {
      v = *reinterpret_cast<u16 *>(glob->init_value);
    } else if (out_bitwidth <= 32) {
      v = *reinterpret_cast<u32 *>(glob->init_value);
    } else {
      v = *reinterpret_cast<u64 *>(glob->init_value);
    }
    return val(ctx->get_constant_int(APInt(out_bitwidth, v)));
  }
  if (load_type->is_float()) {
    if (out_bitwidth == 32) {
      return val(ctx->get_constant_value(
          *reinterpret_cast<f32 *>(glob->init_value), ctx->get_float_type(32)));
    }
    if (out_bitwidth == 64) {
      return val(ctx->get_constant_value(
          *reinterpret_cast<f64 *>(glob->init_value), ctx->get_float_type(64)));
    }
    TODO("prob unsupported");
  }
  if (load_type->is_vec()) {
    // TODO: implement
    return std::nullopt;
  }
  if (load_type->is_ptr()) {
    const u64 v = *reinterpret_cast<u64 *>(glob->init_value);
    return val(ctx->get_constant_int(APInt(out_bitwidth, v)));
  }
  return std::nullopt;
}

Res fold_global_offset_load(fir::Instr instr, fir::Context &ctx) {
  if (!instr->args[0].is_instr()) {
    return std::nullopt;
  }
  auto arg_instr = instr->args[0].as_instr();
  if (!arg_instr->is(fir::InstrType::BinaryInstr) ||
      arg_instr->subtype != static_cast<u32>(BinSub::PtrAdd) ||
      !arg_instr->args[0].is_constant() || !arg_instr->args[1].is_constant()) {
    return std::nullopt;
  }
  auto base_global = arg_instr->args[0].as_constant();
  auto offset = arg_instr->args[1].as_constant();
  auto res_ty = instr->result_type();
  auto load_size = res_ty->get_size();
  auto load_width = res_ty->get_bitwidth();
  if (!base_global->is_global() || !offset->is_int()) {
    return std::nullopt;
  }
  auto offset_v = offset->as_int();
  auto global_v = base_global->as_global();
  if (!global_v->is_constant) {
    return std::nullopt;
  }
  bool any_reloc_overlap = false;
  for (auto reloc : global_v->reloc_info) {
    if ((reloc.insert_offset <= (offset_v + load_size) &&
         reloc.insert_offset + load_size >= offset_v)) {
      any_reloc_overlap = true;
    }
    if (reloc.insert_offset == offset_v && instr->result_type()->is_ptr()) {
      return fir::ValueR{reloc.ref};
    }
  }
  // out of bounds / uninitialized reads are not foldable
  const bool in_bounds =
      global_v->init_value != nullptr && offset_v >= 0 &&
      static_cast<u64>(offset_v) + load_size <= global_v->n_bytes;
  // vectors and structs are not folded
  const bool scalar_ty =
      res_ty->is_int() || res_ty->is_ptr() || res_ty->is_float();
  if (any_reloc_overlap || !in_bounds || !scalar_ty) {
    return std::nullopt;
  }
  auto *data_ptr = &global_v->init_value[offset_v];
  if (res_ty->is_int() || res_ty->is_ptr()) {
    if (load_width == 8) {
      return val(ctx->get_constant_value(
          static_cast<i128>(*reinterpret_cast<i8 *>(data_ptr)), res_ty));
    }
    if (load_width == 16) {
      return val(ctx->get_constant_value(
          static_cast<i128>(*reinterpret_cast<i16 *>(data_ptr)), res_ty));
    }
    if (load_width == 32) {
      return val(ctx->get_constant_value(
          static_cast<i128>(*reinterpret_cast<i32 *>(data_ptr)), res_ty));
    }
    if (load_width == 64) {
      return val(ctx->get_constant_value(
          static_cast<i128>(*reinterpret_cast<i64 *>(data_ptr)), res_ty));
    }
    return std::nullopt;
  }
  if (load_width == 32) {
    return val(
        ctx->get_constant_value(*reinterpret_cast<f32 *>(data_ptr), res_ty));
  }
  if (load_width == 64) {
    return val(
        ctx->get_constant_value(*reinterpret_cast<f64 *>(data_ptr), res_ty));
  }
  return std::nullopt;
}

Res fold_load(fir::Instr instr, fir::Context &ctx) {
  if (instr->args[0].is_constant() && !instr->Volatile) {
    const auto arg0_const = instr->args[0].as_constant();
    if (arg0_const->is_poison() || arg0_const->is_null() ||
        (arg0_const->is_int() && arg0_const->as_int_sat() <= 0)) {
      return poison_of(instr, ctx);
    }
    if (arg0_const->is_global()) {
      if (auto r = fold_global_load(instr, ctx)) {
        return r;
      }
    }
  }
  return fold_global_offset_load(instr, ctx);
}

Res fold_extract(fir::Instr instr) {
  if (instr->args[0].is_constant() && instr->args[0].as_constant()->is_vec() &&
      instr->args[1].is_constant_int()) {
    const auto &members = instr->args[0].as_constant()->as_vec().members;
    auto index = instr->args[1].as_constant()->as_int();
    if (index >= 0 && static_cast<size_t>(index) < members.size()) {
      return fir::ValueR{members[static_cast<size_t>(index)]};
    }
    return std::nullopt;
  }
  if (!instr->args[0].is_instr()) {
    return std::nullopt;
  }
  auto argi = instr->args[0].as_instr();
  if (argi->is(fir::VectorISubType::Broadcast)) {
    return argi->args[0];
  }
  if (argi->is(fir::InstrType::InsertElement) &&
      instr->args[1].is_constant_int() && argi->args[2].is_constant_int() &&
      // the indices can have different integer types
      instr->args[1].as_constant()->as_int() ==
          argi->args[2].as_constant()->as_int()) {
    return argi->args[1];
  }
  return std::nullopt;
}

}  // namespace

Res fold(fir::Instr instr, fir::Context &ctx, AttributerManager &man) {
  switch (instr->get_instr_type()) {
    case fir::InstrType::BinaryInstr:
      return fold_binary(instr, ctx);
    case fir::InstrType::UnaryInstr:
      return fold_unary(instr, ctx);
    case fir::InstrType::ICmp:
      return fold_icmp(instr, ctx, man);
    case fir::InstrType::FCmp:
      return fold_fcmp(instr, ctx);
    case fir::InstrType::SelectInstr:
      return fold_select(instr, ctx);
    case fir::InstrType::SExt:
    case fir::InstrType::ZExt:
      return fold_extend(instr, ctx);
    case fir::InstrType::ITrunc:
      return fold_itrunc(instr, ctx);
    case fir::InstrType::Conversion:
      return fold_conversion(instr, ctx);
    case fir::InstrType::Intrinsic:
      return fold_intrinsic(instr, ctx);
    case fir::InstrType::LoadInstr:
      return fold_load(instr, ctx);
    case fir::InstrType::ExtractElement:
      return fold_extract(instr);
    case fir::InstrType::InsertElement:
      if (instr->args[0].is_poison() && instr->args[1].is_poison()) {
        return poison_of(instr, ctx);
      }
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

}  // namespace InstFold

PreservedAnalysis InstFoldPass::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("InstFold");
  AttributerManager man;
  bool changed = false;

  TVec<fir::Instr> worklist;
  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->get_instrs()) {
      worklist.push_back(instr);
    }
  }
  while (!worklist.empty()) {
    auto instr = worklist.back();
    worklist.pop_back();
    if (!instr.is_valid() || !instr->parent.is_valid()) {
      continue;
    }
    auto folded = InstFold::fold(instr, ctx, man);
    if (!folded) {
      continue;
    }
    man.reset();
    for (auto &use : instr->uses) {
      worklist.push_back(use.user);
    }
    instr->replace_all_uses(*folded);
    instr.destroy();
    changed = true;
  }
  // the known bits analysis sets nsw/nuw on instructions, whatever asked
  changed |= man.ir_modified;
  return changed ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
