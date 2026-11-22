#pragma once
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/value.hpp"

namespace foptim::optim {

inline bool sat_is_native(fir::TypeR ty) {
  if (!ty->is_vec()) {
    return false;
  }
  const auto &v = ty->as_vec();
  return v.type == fir::VectorType::SubType::Integer &&
         (v.bitwidth == 8 || v.bitwidth == 16) &&
         (v.get_size() == 16 || v.get_size() == 32);
}

inline bool is_sat_intrinsic(fir::IntrinsicSubType sub) {
  return sub == fir::IntrinsicSubType::UAddSat ||
         sub == fir::IntrinsicSubType::USubSat ||
         sub == fir::IntrinsicSubType::SAddSat ||
         sub == fir::IntrinsicSubType::SSubSat;
}

// the saturating operation written with compares, selects and min / max, for
// integers and integer vectors of at most 64 bit lanes (wider scalars are split
// into limbs after this)
inline fir::ValueR expand_sat(fir::Context &ctx, fir::Builder &b,
                              fir::IntrinsicSubType sub, fir::ValueR x,
                              fir::ValueR y) {
  using Sub = fir::IntrinsicSubType;
  using BinSub = fir::BinaryInstrSubType;
  using Cmp = fir::ICmpInstrSubType;
  const auto ty = x.get_type();
  const bool is_vec = ty->is_vec();
  const u32 bits = is_vec ? ty->as_vec().bitwidth : ty->get_bitwidth();
  // 64 bit lanes have no unsigned min before avx512vl
  const bool use_min = is_vec && bits < 64;
  auto cst = [&](i128 v) {
    auto c = fir::ValueR{ctx->get_constant_int(v, bits)};
    return is_vec ? b.build_vbroadcast(c, ty) : c;
  };
  const i128 smax = (static_cast<i128>(1) << (bits - 1)) - 1;
  switch (sub) {
    case Sub::UAddSat: {
      if (use_min) {
        // x + y saturates to all ones, which is umin(x, ~y) + y
        const auto not_y = b.build_unary_op(y, fir::UnaryInstrSubType::Not);
        return b.build_int_add(b.build_intrinsic(x, not_y, Sub::UMin), y);
      }
      const auto sum = b.build_int_add(x, y);
      const auto wrapped = b.build_int_cmp(sum, x, Cmp::ULT);
      return b.build_select(ty, wrapped, cst(-1), sum);
    }
    case Sub::USubSat: {
      if (use_min) {
        return b.build_int_sub(x, b.build_intrinsic(x, y, Sub::UMin));
      }
      const auto diff = b.build_int_sub(x, y);
      const auto below = b.build_int_cmp(x, y, Cmp::ULT);
      return b.build_select(ty, below, cst(0), diff);
    }
    case Sub::SAddSat:
    case Sub::SSubSat: {
      const bool add = sub == Sub::SAddSat;
      const auto res = add ? b.build_int_add(x, y) : b.build_int_sub(x, y);
      // add overflows when both operands differ in sign from the result, sub
      // when the operands differ in sign and the result differs from the
      // minuend
      const auto first = b.build_binary_op(x, add ? res : y, BinSub::Xor);
      const auto second = add ? b.build_binary_op(y, res, BinSub::Xor)
                              : b.build_binary_op(x, res, BinSub::Xor);
      const auto both = b.build_binary_op(first, second, BinSub::And);
      const auto overflow = b.build_int_cmp(both, cst(0), Cmp::SLT);
      // the overflow goes the way of the sign of the minuend
      const auto neg = b.build_int_cmp(x, cst(0), Cmp::SLT);
      const auto limit = b.build_select(ty, neg, cst(-smax - 1), cst(smax));
      return b.build_select(ty, overflow, limit, res);
    }
    default:
      UNREACH();
  }
}

}  // namespace foptim::optim
