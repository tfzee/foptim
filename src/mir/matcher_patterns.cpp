#include "matcher_patterns.hpp"

#include <fmt/base.h>
#include <fmt/core.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>

#include "ir/basic_block.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "matcher.hpp"
#include "matcher_pm.hpp"
#include "mir/instr.hpp"
#include "mir/matcher_helpers.hpp"
#include "utils/helpers.hpp"
#include "utils/stats.hpp"

namespace foptim::fmir {

namespace {
bool is_reg(fir::ValueR val) {
  if (!val.is_constant()) {
    return true;
  }
  auto consti = val.as_constant();
  return consti->is_global() || consti->is_func();
}

// integers, null and poison are immediates, everything else is in a register
bool is_imm(fir::ValueR val) { return !is_reg(val); }

bool is_non_negative_imm(fir::ValueR val) {
  if (!is_imm(val)) {
    return false;
  }
  return !val.as_constant()->is_int() || val.as_constant()->as_int() >= 0;
}

bool is_defined_global(fir::ValueR val) {
  return val.is_constant_global() &&
         !val.as_constant()->as_global()->is_extern_decl();
}

// not a vector of pointers
bool is_scalar(fir::TypeR type) { return !type->is_vec(); }

// the operand is a value that lives in a register
const auto Reg = pm::where(is_reg, pm::Any);

auto RegV(fir::ValueR &out) { return pm::where(is_reg, pm::value(out)); }
auto ImmV(fir::ValueR &out) { return pm::where(is_imm, pm::value(out)); }
auto NonNegImmV(fir::ValueR &out) {
  return pm::where(is_non_negative_imm, pm::value(out));
}
auto DefinedGlobalV(fir::ValueR &out) {
  return pm::where(is_defined_global, pm::value(out));
}

// fits the signed 32 bit displacement of an x86 address
bool fits_disp32(i128 v) {
  return v >= std::numeric_limits<i32>::min() &&
         v <= std::numeric_limits<i32>::max();
}

bool is_addr_scale(i128 v) { return v == 1 || v == 2 || v == 4 || v == 8; }

// log2 of the scale of an x86 address
i128 addr_scale_shift(i128 scale) {
  return std::countr_zero(static_cast<u64>(scale));
}

// the predicate immediate of vcmpps / vcmpss
u8 vcmp_predicate(fir::Instr cmp_instr) {
  u8 vcmp_condition = 0;
  switch (static_cast<fir::FCmpInstrSubType>(cmp_instr->get_instr_subtype())) {
    case fir::FCmpInstrSubType::OEQ:
      vcmp_condition = 0x00;
      break;
    case fir::FCmpInstrSubType::OLT:
      vcmp_condition = 0x01;
      break;
    case fir::FCmpInstrSubType::OLE:
      vcmp_condition = 0x02;
      break;
    case fir::FCmpInstrSubType::UNO:
      vcmp_condition = 0x03;
      break;
    case fir::FCmpInstrSubType::UNE:
      vcmp_condition = 0x04;
      break;
    // not greater or equal / not greater than, true for unordered
    case fir::FCmpInstrSubType::ULT:
      vcmp_condition = 0x19;
      break;
    case fir::FCmpInstrSubType::ULE:
      vcmp_condition = 0x1A;
      break;
    case fir::FCmpInstrSubType::ORD:
      vcmp_condition = 0x07;
      break;
    case fir::FCmpInstrSubType::UEQ:
      vcmp_condition = 0x08;
      break;
    case fir::FCmpInstrSubType::AlwFalse:
      vcmp_condition = 0x0B;
      break;
    case fir::FCmpInstrSubType::ONE:
      vcmp_condition = 0x0C;
      break;
    case fir::FCmpInstrSubType::OGE:
      vcmp_condition = 0x0D;
      break;
    case fir::FCmpInstrSubType::OGT:
      vcmp_condition = 0x0E;
      break;
    case fir::FCmpInstrSubType::AlwTrue:
      vcmp_condition = 0x0F;
      break;
    case fir::FCmpInstrSubType::UGT:
      vcmp_condition = 0x16;
      break;
    case fir::FCmpInstrSubType::UGE:
      vcmp_condition = 0x15;
      break;
    case fir::FCmpInstrSubType::IsNaN:
      vcmp_condition = 0x03;
      break;
    case fir::FCmpInstrSubType::INVALID:
      fmt::println("{}", cmp_instr);
      TODO("TODO");
  }

  return vcmp_condition;
}

// all lanes set
MArgument vec_all_ones(MArgument like, MatchResult &res, ExtraMatchData &data) {
  auto ones = MArgument(data.alloc.get_new_register(like.ty), like.ty);
  res.result.emplace_back(X86Subtype::vpcmpeq, ones, ones, ones);
  return ones;
}

// bitwise not of a mask
void vec_not(MArgument dst, MArgument src, MatchResult &res,
             ExtraMatchData &data) {
  res.result.emplace_back(GVecSubtype::vXor, dst, src,
                          vec_all_ones(dst, res, data));
}

// Mask of a vector integer compare, every lane is all ones or all zeros.
// Only equal and signed greater than exist, the rest is built from those.
void vector_icmp(fir::Instr cmp_instr, MArgument dst, MArgument a, MArgument b,
                 MatchResult &res, ExtraMatchData &data) {
  using Sub = fir::ICmpInstrSubType;
  const auto sub = static_cast<Sub>(cmp_instr->get_instr_subtype());
  auto fresh = [&]() {
    return MArgument(data.alloc.get_new_register(a.ty), a.ty);
  };
  const bool wide = a.ty == Type::Int64x2 || a.ty == Type::Int64x4;
  auto gt = [&](MArgument d, MArgument x, MArgument y) {
    res.result.emplace_back(X86Subtype::vpcmpgt, d, x, y);
  };
  // x <= y unsigned is min(x, y) == x
  auto ule = [&](MArgument d, MArgument x, MArgument y) {
    auto m = fresh();
    res.result.emplace_back(GVecSubtype::vUMin, m, x, y);
    res.result.emplace_back(X86Subtype::vpcmpeq, d, m, x);
  };
  // no 64 bit unsigned min, flip the sign bit and compare signed
  auto flip = [&](MArgument sign, MArgument x) {
    auto out = fresh();
    res.result.emplace_back(GVecSubtype::vXor, out, x, sign);
    return out;
  };
  auto negated = [&](auto &&emit) {
    auto t = fresh();
    emit(t);
    vec_not(dst, t, res, data);
  };
  switch (sub) {
    case Sub::EQ:
      res.result.emplace_back(X86Subtype::vpcmpeq, dst, a, b);
      return;
    case Sub::NE:
      negated([&](MArgument t) {
        res.result.emplace_back(X86Subtype::vpcmpeq, t, a, b);
      });
      return;
    case Sub::SGT:
      gt(dst, a, b);
      return;
    case Sub::SLT:
      gt(dst, b, a);
      return;
    case Sub::SGE:
      negated([&](MArgument t) { gt(t, b, a); });
      return;
    case Sub::SLE:
      negated([&](MArgument t) { gt(t, a, b); });
      return;
    case Sub::UGT:
    case Sub::ULT:
    case Sub::UGE:
    case Sub::ULE: {
      if (wide) {
        auto sign = fresh();
        res.result.emplace_back(X86Subtype::psll, sign,
                                vec_all_ones(a, res, data), MArgument(u8{63}));
        auto fa = flip(sign, a);
        auto fb = flip(sign, b);
        switch (sub) {
          case Sub::UGT:
            gt(dst, fa, fb);
            return;
          case Sub::ULT:
            gt(dst, fb, fa);
            return;
          case Sub::UGE:
            negated([&](MArgument t) { gt(t, fb, fa); });
            return;
          default:
            negated([&](MArgument t) { gt(t, fa, fb); });
            return;
        }
      }
      switch (sub) {
        case Sub::ULE:
          ule(dst, a, b);
          return;
        case Sub::UGE:
          ule(dst, b, a);
          return;
        case Sub::UGT:
          negated([&](MArgument t) { ule(t, a, b); });
          return;
        default:
          negated([&](MArgument t) { ule(t, b, a); });
          return;
      }
    }
    case Sub::INVALID:
      UNREACH();
  }
}

// Inserts the scalar input as lane lane of the vector input_vec. The lane is
// patched in the 128 bit half it lives in.
void insert_lane_generic(fir::Instr insert_instr, MatchResult &res,
                         ExtraMatchData &data, MArgument target,
                         MArgument input_vec, MArgument input, u32 lane) {
  const u32 elem_bytes = get_size(input.ty);
  const u32 lanes_per_half = 16 / elem_bytes;
  const u32 half = lane / lanes_per_half;
  const u32 j = lane % lanes_per_half;
  Type xmm_ty = Type::INVALID;
  switch (input.ty) {
    case Type::Int8:
      xmm_ty = Type::Int8x16;
      break;
    case Type::Int16:
      xmm_ty = Type::Int16x8;
      break;
    case Type::Int32:
      xmm_ty = Type::Int32x4;
      break;
    case Type::Int64:
      xmm_ty = Type::Int64x2;
      break;
    case Type::Float32:
      xmm_ty = Type::Float32x4;
      break;
    default:
      fmt::println("{:cd}", insert_instr);
      TODO("insertelement of this element type");
  }
  res.result.emplace_back(GBaseSubtype::mov, target, input_vec);
  auto patch = [&](MArgument dst) {
    if (input.ty == Type::Float32) {
      res.result.emplace_back(X86Subtype::vinsertps, dst, dst,
                              MArgument(input.reg.retype(xmm_ty), xmm_ty),
                              MArgument(static_cast<u8>(j << 4)));
      return;
    }
    auto src = input;
    if (input.ty == Type::Int8 || input.ty == Type::Int16) {
      // vpinsrb / vpinsrw read a 32 bit register
      src = MArgument(data.alloc.get_new_register(Type::Int32), Type::Int32);
      res.result.emplace_back(GConvSubtype::mov_zx, src, input);
    }
    res.result.emplace_back(X86Subtype::vpinsr, dst, dst, src,
                            MArgument(static_cast<u8>(j)));
  };
  if (get_size(target.ty) == 32) {
    auto h = MArgument(data.alloc.get_new_register(xmm_ty), xmm_ty);
    res.result.emplace_back(X86Subtype::vextract128, h, target,
                            MArgument(static_cast<u8>(half)));
    patch(h);
    res.result.emplace_back(X86Subtype::vinsert128, target, target, h,
                            MArgument(static_cast<u8>(half)));
  } else {
    patch(target);
  }
}

void move_rules(RuleSet &rules) {
  rules.add(pm::Select, [](fir::Instr slct_instr, MatchResult &res,
                           ExtraMatchData &data) {
    // handle dumb ? 1 : 0 or ? 0 : 1 patterns that can get generated by
    // optimizations but sometimes arnt cleaned up
    fir::Instr cmp_instr;
    i128 c1;
    i128 c2;
    if (!pm::match(slct_instr,
                   pm::Select(pm::capture(cmp_instr, pm::ICmp()),
                              pm::const_int(c1), pm::const_int(c2))) ||
        !slct_instr->result_type()->is_int()) {
      return false;
    }

    auto dir1 = (c1 != 0 && c2 == 0);
    auto dir2 = (c1 == 0 && c2 != 0);
    // no direct imul on i8
    if (slct_instr->result_type()->as_int() == 8 &&
        ((!dir1 || !foptim::utils::is_pow2(c1)) &&
         (!dir2 || !foptim::utils::is_pow2(c2)))) {
      return false;
    }
    if (!dir1 && !dir2) {
      return false;
    }
    auto res_arg = valueToArg(slct_instr.result(), res.result, data.alloc);
    auto arg1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
    auto arg2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);
    GJumpSubtype op = GJumpSubtype::icmp_eq;

    switch (
        static_cast<fir::ICmpInstrSubType>(cmp_instr->get_instr_subtype())) {
      case fir::ICmpInstrSubType::SLT:
        op = dir1 ? GJumpSubtype::icmp_slt : GJumpSubtype::icmp_sge;
        break;
      case fir::ICmpInstrSubType::ULT:
        op = dir1 ? GJumpSubtype::icmp_ult : GJumpSubtype::icmp_uge;
        break;
      case fir::ICmpInstrSubType::NE:
        op = dir1 ? GJumpSubtype::icmp_ne : GJumpSubtype::icmp_eq;
        break;
      case fir::ICmpInstrSubType::EQ:
        op = dir1 ? GJumpSubtype::icmp_eq : GJumpSubtype::icmp_ne;
        break;
      case fir::ICmpInstrSubType::SGT:
        op = dir1 ? GJumpSubtype::icmp_sgt : GJumpSubtype::icmp_sle;
        break;
      case fir::ICmpInstrSubType::UGT:
        op = dir1 ? GJumpSubtype::icmp_ugt : GJumpSubtype::icmp_ule;
        break;
      case fir::ICmpInstrSubType::UGE:
        op = dir1 ? GJumpSubtype::icmp_uge : GJumpSubtype::icmp_ult;
        break;
      case fir::ICmpInstrSubType::ULE:
        op = dir1 ? GJumpSubtype::icmp_ule : GJumpSubtype::icmp_ugt;
        break;
      case fir::ICmpInstrSubType::SGE:
        op = dir1 ? GJumpSubtype::icmp_sge : GJumpSubtype::icmp_slt;
        break;
      case fir::ICmpInstrSubType::SLE:
        op = dir1 ? GJumpSubtype::icmp_sle : GJumpSubtype::icmp_sgt;
        break;
      case fir::ICmpInstrSubType::INVALID:
        UNREACH();
    }
    // Do a little cheating
    auto res_arg_smol = res_arg;
    res_arg_smol.reg.ty = Type::Int8;
    res.result.emplace_back(op, res_arg_smol, arg1, arg2);
    // scale by c1/c2
    u64 v = dir1 ? static_cast<u64>(c1) : static_cast<u64>(c2);
    if (v > 1) {
      if (foptim::utils::is_pow2(v)) {
        res.result.emplace_back(
            GArithSubtype::shl2, res_arg,
            MArgument(static_cast<u64>(64 - __builtin_clzg(v - 1))));
      } else {
        res.result.emplace_back(GArithSubtype::mul2, res_arg, MArgument(v));
      }
    }

    return true;
  });
  rules.add(pm::Select, [](fir::Instr slct_instr, MatchResult &res,
                           ExtraMatchData &data) {
    fir::Instr cmp_instr;
    if (!pm::match(slct_instr,
                   pm::Select(pm::capture(cmp_instr, pm::ICmp())))) {
      return false;
    }

    auto res_arg = valueToArg(slct_instr.result(), res.result, data.alloc);
    if (res_arg.is_vec_reg() || res_arg.ty == Type::Int8) {
      return false;
    }
    auto c1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
    auto c2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);
    auto arg1 = valueToArg(slct_instr->args[1], res.result, data.alloc);
    auto arg2 = valueToArg(slct_instr->args[2], res.result, data.alloc);
    GCMovSubtype op = GCMovSubtype::cmov;

    switch (
        static_cast<fir::ICmpInstrSubType>(cmp_instr->get_instr_subtype())) {
      case fir::ICmpInstrSubType::SGT:
        op = GCMovSubtype::cmov_sgt;
        break;
      case fir::ICmpInstrSubType::SLT:
        op = GCMovSubtype::cmov_slt;
        break;
      case fir::ICmpInstrSubType::ULT:
        op = GCMovSubtype::cmov_ult;
        break;
      case fir::ICmpInstrSubType::SGE:
        op = GCMovSubtype::cmov_sge;
        break;
      case fir::ICmpInstrSubType::SLE:
        op = GCMovSubtype::cmov_sle;
        break;
      case fir::ICmpInstrSubType::NE:
        op = GCMovSubtype::cmov_ne;
        break;
      case fir::ICmpInstrSubType::EQ:
        op = GCMovSubtype::cmov_eq;
        break;
      case fir::ICmpInstrSubType::UGT:
        op = GCMovSubtype::cmov_ugt;
        break;
      case fir::ICmpInstrSubType::UGE:
        op = GCMovSubtype::cmov_uge;
        break;
      case fir::ICmpInstrSubType::ULE:
        op = GCMovSubtype::cmov_ule;
        break;
      case fir::ICmpInstrSubType::INVALID:
        UNREACH();
    }

    res.result.emplace_back(GBaseSubtype::mov, res_arg, arg2);
    res.result.emplace_back(op, res_arg, arg1, c1, c2);
    return true;
  });
  rules.add(pm::Select, [](fir::Instr slct_instr, MatchResult &res,
                           ExtraMatchData &data) {
    fir::Instr cmp_instr;
    if (!pm::match(slct_instr,
                   pm::Select(pm::capture(cmp_instr, pm::FCmp())))) {
      return false;
    }

    auto res_arg = valueToArg(slct_instr.result(), res.result, data.alloc);
    auto c1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
    auto c2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);
    auto arg1 = valueToArg(slct_instr->args[1], res.result, data.alloc);
    auto arg2 = valueToArg(slct_instr->args[2], res.result, data.alloc);

    auto cond_ty = Type::INVALID;
    switch (res_arg.ty) {
      case Type::Float32:
        cond_ty = Type::Int32x4;
        break;
      case Type::Float64:
        cond_ty = Type::Int64x2;
        break;
      case Type::Int8:
      case Type::Int16:
      case Type::Int32:
      case Type::Int64:
        // TOOD: prob cant do it efficiently?
        // return false;
      case Type::Float32x2:
      case Type::Int32x4:
      case Type::Int64x2:
      case Type::Float32x4:
      case Type::Float64x2:
      case Type::Int32x8:
      case Type::Int64x4:
      case Type::Float32x8:
      case Type::Float64x4:
      case Type::Int32x16:
      case Type::Int64x8:
      case Type::Float32x16:
      case Type::Float64x8:
      case Type::Int8x16:
      case Type::Int16x8:
      case Type::Int8x32:
      case Type::Int16x16:
      case Type::Int8x4:
      case Type::Int8x8:
      case Type::Int16x4:
      case Type::Int32x2:
        return false;
      case Type::INVALID:
        fmt::println("{}", cmp_instr);
        fmt::println("{}", slct_instr);
        fmt::println("{}", res_arg);
        TODO("Impl");
    }
    auto condition = MArgument(data.alloc.get_new_register(cond_ty), cond_ty);

    const u8 vcmp_condition = vcmp_predicate(cmp_instr);

    res.result.emplace_back(X86Subtype::vcmp, condition, c1, c2,
                            MArgument(vcmp_condition));
    // vblendv picks the second source where the mask is set, so the
    // false value goes first
    res.result.emplace_back(X86Subtype::vblendv, res_arg, arg2, arg1,
                            condition);
    return true;
  });
}
// Truncates the lanes of an integer vector to fewer bits, the result holds the
// lanes in its low bytes. 64 to 32 bit picks the even dwords, everything else
// masks the lanes and halves them with unsigned saturating packs, which are
// exact after the mask. The two halves of a ymm are packed together.
static void lower_vector_itrunc(MatchResult &res, ExtraMatchData &data,
                                MArgument dst, MArgument src) {
  auto int_vec = [](u32 bits, bool wide) {
    switch (bits) {
      case 8:
        return wide ? Type::Int8x32 : Type::Int8x16;
      case 16:
        return wide ? Type::Int16x16 : Type::Int16x8;
      case 32:
        return wide ? Type::Int32x8 : Type::Int32x4;
      case 64:
        return wide ? Type::Int64x4 : Type::Int64x2;
      default:
        UNREACH();
    }
  };
  auto fresh = [&](Type ty) {
    return MArgument{data.alloc.get_new_register(ty), ty};
  };
  if (!src.isReg()) {
    auto in_reg = fresh(src.ty);
    res.result.emplace_back(GBaseSubtype::mov, in_reg, src);
    src = in_reg;
  }
  u32 bits = vec_lane_bits(src.ty);
  const u32 to_bits = vec_lane_bits(dst.ty);
  ASSERT(to_bits != 0 && to_bits < bits);
  auto cur = src;
  // the two xmm halves of cur, or the xmm and a dummy copy of it
  auto halves = [&](MArgument v) {
    const auto xmm = int_vec(vec_lane_bits(v.ty), false);
    auto lo = MArgument{v.reg.retype(xmm), xmm};
    if (get_size(v.ty) != 32) {
      return std::pair{lo, lo};
    }
    auto hi = fresh(xmm);
    res.result.emplace_back(X86Subtype::vextract128, hi, v,
                            MArgument(static_cast<u8>(1)));
    return std::pair{lo, hi};
  };
  auto out_arg = [&](u32 out_bits) {
    return out_bits == to_bits ? dst : fresh(int_vec(out_bits, false));
  };

  if (bits == 64) {
    auto [lo, hi] = halves(cur);
    auto next = out_arg(32);
    res.result.emplace_back(X86Subtype::vshufps, next, lo, hi,
                            MArgument(static_cast<u8>(0x88)));
    cur = next;
    bits = 32;
  }
  if (bits > to_bits) {
    // a partial source is read as the full register of its lane width
    const auto full = int_vec(bits, get_size(cur.ty) == 32);
    auto ones = fresh(full);
    res.result.emplace_back(X86Subtype::vpcmpeq, ones, ones, ones);
    res.result.emplace_back(X86Subtype::psrl, ones, ones,
                            MArgument(static_cast<u8>(bits - to_bits)));
    auto masked = fresh(full);
    res.result.emplace_back(GVecSubtype::vAnd, masked,
                            MArgument{cur.reg.retype(full), full}, ones);
    cur = masked;
  }
  while (bits > to_bits) {
    auto [lo, hi] = halves(cur);
    auto next = out_arg(bits / 2);
    res.result.emplace_back(X86Subtype::vpack, next, lo, hi);
    cur = next;
    bits /= 2;
  }
}

// poison or all lanes zero, the value of the lanes a masked load skips
bool is_zero_or_poison(fir::ValueR v) {
  if (!v.is_constant()) {
    return false;
  }
  auto c = v.as_constant();
  if (c->is_poison()) {
    return true;
  }
  if (c->is_int()) {
    return c->as_int() == 0;
  }
  if (c->is_float()) {
    return std::bit_cast<u64>(c->as_f64()) == 0;
  }
  if (c->is_vec()) {
    for (auto m : c->as_vec().members) {
      if (!is_zero_or_poison(fir::ValueR{m})) {
        return false;
      }
    }
    return true;
  }
  return false;
}

// integer vector type of the same shape
Type int_vec_like(Type ty) {
  switch (ty) {
    case Type::Float32x4:
      return Type::Int32x4;
    case Type::Float32x8:
      return Type::Int32x8;
    case Type::Float64x2:
      return Type::Int64x2;
    case Type::Float64x4:
      return Type::Int64x4;
    default:
      return ty;
  }
}

void memory_rules(RuleSet &rules) {
  rules.add(pm::MaskedLoad, [](fir::Instr load_instr, MatchResult &res,
                               ExtraMatchData &data) {
    auto res_reg = valueToArg(load_instr.result(), res.result, data.alloc);
    auto mask = valueToArg(load_instr->args[1], res.result, data.alloc);
    auto mem =
        valueToArgPtr(load_instr->args[0], res_reg.ty, res.result, data.alloc);
    mem.ty = res_reg.ty;
    if (is_zero_or_poison(load_instr->args[2])) {
      res.result.emplace_back(X86Subtype::vmaskload, res_reg, mask, mem);
      return true;
    }
    // the skipped lanes are zero, take the pass through value there
    auto loaded =
        MArgument(data.alloc.get_new_register(res_reg.ty), res_reg.ty);
    auto passthru = valueToArg(load_instr->args[2], res.result, data.alloc);
    res.result.emplace_back(X86Subtype::vmaskload, loaded, mask, mem);
    res.result.emplace_back(X86Subtype::vblendv, res_reg, passthru, loaded,
                            mask);
    return true;
  });
  rules.add(pm::MaskedStore, [](fir::Instr store_instr, MatchResult &res,
                                ExtraMatchData &data) {
    auto value = valueToArg(store_instr->args[2], res.result, data.alloc);
    auto mask = valueToArg(store_instr->args[1], res.result, data.alloc);
    auto mem =
        valueToArgPtr(store_instr->args[0], value.ty, res.result, data.alloc);
    mem.ty = value.ty;
    res.result.emplace_back(X86Subtype::vmaskstore, mem, mask, value);
    return true;
  });
  auto inline_asm_rule = [](fir::Instr asm_instr, MatchResult &res,
                            ExtraMatchData &data) {
    const auto id = static_cast<u32>(asm_instr->args[0].as_constant()->as_int());
    const auto &info = asm_instr->get_parent()->get_parent()->ctx->asm_info(id);
    const auto op_i32 = [](size_t i) {
      return MArgument::Int(i, Type::Int32);
    };

    TVec<MInstr> ins;
    for (size_t op_id = 0; op_id < info.operands.size(); op_id++) {
      const auto &op = info.operands[op_id];
      if (op.kind != fir::AsmOperandKind::Input &&
          !(op.kind == fir::AsmOperandKind::Output && op.indirect)) {
        continue;
      }
      auto val = asm_instr->args[static_cast<size_t>(op.arg) + 1];
      MArgument arg;
      if (op.cls == fir::AsmOperandClass::Imm) {
        if (!val.is_constant_int()) {
          fmt::println(stderr, "inline asm operand {} is not a constant: {}",
                       op_id, info.tmpl);
          IMPL("inline asm immediate operand is not constant");
        }
        arg = MArgument::Int(static_cast<u64>(val.as_constant()->as_int()),
                             convert_type(val.get_type()));
      } else {
        arg = valueToArg(val, res.result, data.alloc);
      }
      ins.emplace_back(GBaseSubtype::asm_in, arg, op_i32(op_id));
    }

    MAsmBlock block;
    block.tmpl = info.tmpl;
    block.operands = info.operands;
    block.sideeffect = info.sideeffect;
    block.mem_clobber = info.mem_clobber;
    const auto block_id = static_cast<u64>(data.func.asm_blocks.size());
    data.func.asm_blocks.push_back(std::move(block));

    for (const auto &in : ins) {
      res.result.push_back(in);
    }
    res.result.emplace_back(GBaseSubtype::inline_asm,
                            MArgument::Int(block_id, Type::Int32));

    u8 result_id = 0;
    for (size_t op_id = 0; op_id < info.operands.size(); op_id++) {
      const auto &op = info.operands[op_id];
      if (op.kind != fir::AsmOperandKind::Output || op.indirect) {
        continue;
      }
      ASSERT(result_id < asm_instr.n_results());
      auto out = valueToArg(asm_instr.result(result_id), res.result, data.alloc);
      res.result.emplace_back(GBaseSubtype::asm_out, out, op_i32(op_id));
      result_id++;
    }
    return true;
  };
  rules.add(pm::InlineAsm, inline_asm_rule);
  rules.add(pm::InlineAsmPure, inline_asm_rule);
  // the thread pointer plus the offset of the variable in the TLS block: local
  // exec (an immediate the linker fills in) for a variable of this module,
  // initial exec (the offset is read from the GOT) for an external one
  rules.add(pm::TlsAddress, [](fir::Instr instr, MatchResult &res,
                               ExtraMatchData &data) {
    ASSERT(instr->args[0].is_constant() &&
           instr->args[0].as_constant()->is_global());
    auto global = instr->args[0].as_constant()->as_global();
    auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
    auto off = MArgument(data.alloc.get_new_register(Type::Int64), Type::Int64);
    if (global->init_value == nullptr) {
      res.result.emplace_back(GBaseSubtype::mov, off,
                              MArgument::MemL(global->name.c_str(), Type::Int64));
    } else {
      res.result.emplace_back(GBaseSubtype::mov, off,
                              MArgument(global->name.c_str()));
    }
    res.result.emplace_back(X86Subtype::tls_base, res_reg);
    res.result.emplace_back(GArithSubtype::add2, res_reg, off);
    return true;
  });
  rules.add(pm::Gather, [](fir::Instr gather_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(gather_instr.result(), res.result, data.alloc);
    auto base = valueToArg(gather_instr->args[0], res.result, data.alloc);
    auto idx = valueToArg(gather_instr->args[1], res.result, data.alloc);
    const auto elem_size =
        (gather_instr.result_type()->as_vec().bitwidth + 7) / 8;
    ASSERT(base.isReg() && idx.isReg());

    // the instruction needs dst, index and mask in different registers, it
    // reads and clears the mask and keeps the lanes of dst the mask skips
    auto dst = res_reg;
    const bool masked = gather_instr->get_n_args() == 4;
    if (!masked || is_zero_or_poison(gather_instr->args[3])) {
      res.result.emplace_back(GVecSubtype::vXor, dst, dst, dst);
    } else {
      auto passthru = valueToArg(gather_instr->args[3], res.result, data.alloc);
      res.result.emplace_back(GBaseSubtype::mov, dst, passthru);
    }
    auto mask_ty = int_vec_like(res_reg.ty);
    auto mask = MArgument(data.alloc.get_new_register(mask_ty), mask_ty);
    if (!masked) {
      res.result.emplace_back(X86Subtype::vpcmpeq, mask, mask, mask);
    } else {
      auto src = valueToArg(gather_instr->args[2], res.result, data.alloc);
      res.result.emplace_back(GBaseSubtype::mov, mask, src);
    }
    auto mem = MArgument::MemBIS(base.reg, idx.reg, elem_size == 8 ? 3 : 2,
                                 res_reg.ty);
    res.result.emplace_back(X86Subtype::vgather, dst, mem, mask);
    return true;
  });
  rules.add(pm::Load, [](fir::Instr load_instr, MatchResult &res,
                         ExtraMatchData &data) {
    fir::Instr mul_instr;
    fir::Instr add_instr;
    i128 scale;
    if (!pm::match(
            load_instr,
            pm::Load(pm::capture(
                add_instr,
                pm::PtrAdd(
                    Reg, pm::capture(
                             mul_instr,
                             pm::IntMul(Reg, pm::const_int(
                                                 scale, is_addr_scale)))))))) {
      return false;
    }

    auto load_ty = convert_type(load_instr.result_type());
    auto add_ty = convert_type(add_instr.result_type());
    if (load_instr->args[0].get_type()->is_vec()) {
      return false;
    }
    if (add_ty >= Type::Float32) {
      // cant do it on vector regs
      return false;
    }
    auto add_ty_size = get_size(add_ty);
    if (add_ty_size != 8 && add_ty_size != 4) {
      return false;
    }
    const i128 consti_val = addr_scale_shift(scale);

    // $1 = $0 * C
    // R = $2 + $1
    // where $0 and $2 must be regs and C in [1,2,4,8]
    auto res_reg = valueToArg(load_instr.result(), res.result, data.alloc);
    auto base = valueToArg(add_instr->args[0], res.result, data.alloc);
    auto indx = valueToArg(mul_instr->args[0], res.result, data.alloc);
    ASSERT(base.isReg());
    ASSERT(indx.isReg());
    if (add_instr->args[0].is_constant_global()) {
      res.result.emplace_back(
          GBaseSubtype::mov, res_reg,
          MArgument::MemLIS(
              add_instr->args[0].as_constant()->as_global()->name.c_str(),
              indx.reg, consti_val, load_ty));
    }
    res.result.emplace_back(
        GBaseSubtype::mov, res_reg,
        MArgument::MemBIS(base.reg, indx.reg, consti_val, load_ty));
    return true;
  });
  rules.add(pm::Store, [](fir::Instr store_instr, MatchResult &res,
                          ExtraMatchData &data) {
    fir::Instr mul_instr;
    fir::Instr add_instr;
    i128 scale;
    if (!pm::match(
            store_instr,
            pm::Store(pm::capture(
                add_instr,
                pm::PtrAdd(
                    Reg, pm::capture(
                             mul_instr,
                             pm::IntMul(Reg, pm::const_int(
                                                 scale, is_addr_scale)))))))) {
      return false;
    }

    auto store_ty = convert_type(store_instr.result_type());
    auto add_ty = convert_type(add_instr.result_type());
    if (store_instr->args[0].get_type()->is_vec()) {
      return false;
    }
    if (add_ty >= Type::Float32) {
      // cant do it on vector regs
      return false;
    }
    auto add_ty_size = get_size(add_ty);
    if (add_ty_size != 8 && add_ty_size != 4) {
      return false;
    }
    const i128 consti_val = addr_scale_shift(scale);

    // $1 = $0 * C
    // R = $2 + $1
    // where $0 and $2 must be regs and C in [1,2,4,8]
    auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
    auto base = valueToArg(add_instr->args[0], res.result, data.alloc);
    auto indx = valueToArg(mul_instr->args[0], res.result, data.alloc);
    if (!base.isReg() || !indx.isReg()) {
      return false;
    }
    // ASSERT(base.isReg());
    // ASSERT(indx.isReg());
    res.result.emplace_back(
        GBaseSubtype::mov,
        MArgument::MemBIS(base.reg, indx.reg, consti_val, store_ty), value);
    return true;
  });
  // base + index + displacement, the displacement is the operand of the inner
  // add or the outer one
  rules.add(pm::Load, [](fir::Instr load_instr, MatchResult &res,
                         ExtraMatchData &data) {
    fir::ValueR base;
    fir::ValueR disp;
    fir::ValueR indx;
    if (!pm::match(
            load_instr,
            pm::Load(pm::of_type(
                is_scalar,
                pm::PtrAdd(pm::commutative(pm::PtrAdd, RegV(base), ImmV(disp)),
                           RegV(indx)))))) {
      return false;
    }
    auto load_ty = convert_type(load_instr.result_type());
    auto base_arg = valueToArg(base, res.result, data.alloc);
    auto disp_arg = valueToArg(disp, res.result, data.alloc);
    auto indx_arg = valueToArg(indx, res.result, data.alloc);
    auto res_reg = valueToArg(load_instr.result(), res.result, data.alloc);
    res.result.emplace_back(
        GBaseSubtype::mov, res_reg,
        MArgument::MemOBI(disp_arg.imm, base_arg.reg, indx_arg.reg, load_ty));
    return true;
  });
  rules.add(pm::Load, [](fir::Instr load_instr, MatchResult &res,
                         ExtraMatchData &data) {
    fir::ValueR base;
    fir::ValueR indx;
    fir::ValueR disp;
    if (!pm::match(
            load_instr,
            pm::Load(pm::of_type(
                is_scalar,
                pm::commutative(pm::PtrAdd, pm::PtrAdd(RegV(base), RegV(indx)),
                                ImmV(disp)))))) {
      return false;
    }
    auto load_ty = convert_type(load_instr.result_type());
    auto base_arg = valueToArg(base, res.result, data.alloc);
    auto indx_arg = valueToArg(indx, res.result, data.alloc);
    auto disp_arg = valueToArg(disp, res.result, data.alloc);
    auto res_reg = valueToArg(load_instr.result(), res.result, data.alloc);
    res.result.emplace_back(
        GBaseSubtype::mov, res_reg,
        MArgument::MemOBI(disp_arg.imm, base_arg.reg, indx_arg.reg, load_ty));
    return true;
  });
  rules.add(pm::Store, [](fir::Instr store_instr, MatchResult &res,
                          ExtraMatchData &data) {
    fir::ValueR base;
    fir::ValueR disp;
    fir::ValueR indx;
    if (!pm::match(store_instr,
                   pm::Store(pm::of_type(
                       is_scalar,
                       pm::commutative(
                           pm::PtrAdd,
                           pm::commutative(pm::PtrAdd, RegV(base), ImmV(disp)),
                           RegV(indx)))))) {
      return false;
    }
    auto store_ty = convert_type(store_instr.result_type());
    auto base_arg = valueToArg(base, res.result, data.alloc);
    auto disp_arg = valueToArg(disp, res.result, data.alloc);
    auto indx_arg = valueToArg(indx, res.result, data.alloc);
    auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
    res.result.emplace_back(
        GBaseSubtype::mov,
        MArgument::MemOBI(disp_arg.imm, base_arg.reg, indx_arg.reg, store_ty),
        value);
    return true;
  });
  rules.add(pm::Store, [](fir::Instr store_instr, MatchResult &res,
                          ExtraMatchData &data) {
    fir::ValueR base;
    fir::ValueR indx;
    fir::ValueR disp;
    if (!pm::match(
            store_instr,
            pm::Store(pm::of_type(
                is_scalar,
                pm::commutative(pm::PtrAdd, pm::PtrAdd(RegV(base), RegV(indx)),
                                ImmV(disp)))))) {
      return false;
    }
    auto store_ty = convert_type(store_instr.result_type());
    auto base_arg = valueToArg(base, res.result, data.alloc);
    auto indx_arg = valueToArg(indx, res.result, data.alloc);
    auto disp_arg = valueToArg(disp, res.result, data.alloc);
    auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
    res.result.emplace_back(
        GBaseSubtype::mov,
        MArgument::MemOBI(disp_arg.imm, base_arg.reg, indx_arg.reg, store_ty),
        value);
    return true;
  });

  // label + displacement
  rules.add(pm::Load, [](fir::Instr load_instr, MatchResult &res,
                         ExtraMatchData &data) {
    fir::ValueR global;
    fir::ValueR disp;
    if (!pm::match(
            load_instr,
            pm::Load(pm::of_type(
                is_scalar, pm::commutative(pm::PtrAdd, DefinedGlobalV(global),
                                           ImmV(disp)))))) {
      return false;
    }
    auto load_ty = convert_type(load_instr.result_type());
    auto res_reg = valueToArg(load_instr.result(), res.result, data.alloc);
    auto disp_arg = valueToArg(disp, res.result, data.alloc);
    auto label = valueToArgPtr(global, Type::Int64, res.result, data.alloc);
    ASSERT(label.type == MArgument::ArgumentType::MemLabel);
    res.result.emplace_back(
        GBaseSubtype::mov, res_reg,
        MArgument::MemLO(label.label, std::bit_cast<i64>(disp_arg.imm),
                         load_ty));
    return true;
  });
  rules.add(pm::Store, [](fir::Instr store_instr, MatchResult &res,
                          ExtraMatchData &data) {
    fir::ValueR global;
    fir::ValueR disp;
    if (!pm::match(
            store_instr,
            pm::Store(pm::of_type(
                is_scalar, pm::commutative(pm::PtrAdd, DefinedGlobalV(global),
                                           ImmV(disp)))))) {
      return false;
    }
    auto store_ty = convert_type(store_instr.result_type());
    auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
    auto disp_arg = valueToArg(disp, res.result, data.alloc);
    auto label = valueToArgPtr(global, Type::Int64, res.result, data.alloc);
    ASSERT(label.type == MArgument::ArgumentType::MemLabel);
    res.result.emplace_back(
        GBaseSubtype::mov,
        MArgument::MemLO(label.label, std::bit_cast<i64>(disp_arg.imm),
                         store_ty),
        value);
    return true;
  });

  // base + displacement
  rules.add(pm::Load, [](fir::Instr load_instr, MatchResult &res,
                         ExtraMatchData &data) {
    fir::ValueR base;
    fir::ValueR disp;
    if (!pm::match(load_instr,
                   pm::Load(pm::of_type(
                       is_scalar,
                       pm::commutative(pm::PtrAdd, RegV(base), ImmV(disp)))))) {
      return false;
    }
    auto load_ty = convert_type(load_instr.result_type());
    auto res_reg = valueToArg(load_instr.result(), res.result, data.alloc);
    auto base_arg = valueToArg(base, res.result, data.alloc);
    auto disp_arg = valueToArg(disp, res.result, data.alloc);
    res.result.emplace_back(
        GBaseSubtype::mov, res_reg,
        MArgument::MemOB(disp_arg.imm, base_arg.reg, load_ty));
    return true;
  });
  rules.add(pm::Store, [](fir::Instr store_instr, MatchResult &res,
                          ExtraMatchData &data) {
    fir::ValueR base;
    fir::ValueR disp;
    // negative displacements are not folded
    if (!pm::match(store_instr,
                   pm::Store(pm::of_type(is_scalar,
                                         pm::commutative(pm::PtrAdd, RegV(base),
                                                         NonNegImmV(disp)))))) {
      return false;
    }
    auto store_ty = convert_type(store_instr.result_type());
    auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
    auto base_arg = valueToArg(base, res.result, data.alloc);
    auto disp_arg = valueToArg(disp, res.result, data.alloc);
    res.result.emplace_back(
        GBaseSubtype::mov,
        MArgument::MemOB(disp_arg.imm, base_arg.reg, store_ty), value);
    return true;
  });

  // base + index
  rules.add(pm::Load, [](fir::Instr load_instr, MatchResult &res,
                         ExtraMatchData &data) {
    fir::ValueR base;
    fir::ValueR indx;
    if (!pm::match(load_instr,
                   pm::Load(pm::of_type(is_scalar,
                                        pm::PtrAdd(RegV(base), RegV(indx)))))) {
      return false;
    }
    auto load_ty = convert_type(load_instr.result_type());
    auto res_reg = valueToArg(load_instr.result(), res.result, data.alloc);
    auto base_arg = valueToArg(base, res.result, data.alloc);
    auto indx_arg = valueToArg(indx, res.result, data.alloc);
    auto max_type = std::max(base_arg.reg.ty, indx_arg.reg.ty);
    auto base_ext = base_arg.reg;
    auto indx_ext = indx_arg.reg;
    base_ext.ty = max_type;
    indx_ext.ty = max_type;
    res.result.emplace_back(GBaseSubtype::mov, res_reg,
                            MArgument::MemBI(base_ext, indx_ext, load_ty));
    return true;
  });
  rules.add(pm::Store, [](fir::Instr store_instr, MatchResult &res,
                          ExtraMatchData &data) {
    fir::ValueR base;
    fir::ValueR indx;
    if (!pm::match(store_instr,
                   pm::Store(pm::of_type(
                       is_scalar, pm::PtrAdd(RegV(base), RegV(indx)))))) {
      return false;
    }
    auto store_ty = convert_type(store_instr.result_type());
    auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
    auto base_arg = valueToArg(base, res.result, data.alloc);
    auto indx_arg = valueToArg(indx, res.result, data.alloc);
    res.result.emplace_back(
        GBaseSubtype::mov,
        MArgument::MemBI(base_arg.reg, indx_arg.reg, store_ty), value);
    return true;
  });
}

void cjmp_rules(RuleSet &rules) {
  rules.add(pm::CondBranch, [](fir::Instr branch_instr, MatchResult &res,
                               ExtraMatchData &data) {
    fir::Instr and_instr;
    if (!pm::match(branch_instr,
                   pm::CondBranch(pm::capture(and_instr, pm::And())))) {
      return false;
    }

    auto &bb_with_args = branch_instr->bbs[0];
    auto target_bb = branch_instr->bbs[0].bb;
    {
      auto v1 = valueToArg(and_instr->args[0], res.result, data.alloc);
      auto v2 = valueToArg(and_instr->args[1], res.result, data.alloc);
      ASSERT(bb_with_args.args.size() == target_bb->args.size());
      generate_bb_args(bb_with_args, res, data);
      res.result.push_back(MInstr::cJmp_and(v1, v2, data.bbs[bb_with_args.bb]));
    }
    {
      auto bb2_with_args = branch_instr->bbs[1];
      auto target_bb2 = branch_instr->bbs[1].bb;
      ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
      // ASSERT(bb2_with_args.args.size() == 0);
      generate_bb_args(bb2_with_args, res, data);
      res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
    }
    return true;
  });
  rules.add(pm::CondBranch, [](fir::Instr branch_instr, MatchResult &res,
                               ExtraMatchData &data) {
    fir::Instr or_instr;
    if (!pm::match(branch_instr,
                   pm::CondBranch(pm::capture(or_instr, pm::Or())))) {
      return false;
    }

    auto &bb_with_args = branch_instr->bbs[0];
    auto target_bb = branch_instr->bbs[0].bb;
    {
      auto v1 = valueToArg(or_instr->args[0], res.result, data.alloc);
      auto v2 = valueToArg(or_instr->args[1], res.result, data.alloc);
      ASSERT(bb_with_args.args.size() == target_bb->args.size());
      generate_bb_args(bb_with_args, res, data);
      res.result.push_back(MInstr::cJmp_or(v1, v2, data.bbs[bb_with_args.bb]));
    }
    {
      auto bb2_with_args = branch_instr->bbs[1];
      auto target_bb2 = branch_instr->bbs[1].bb;
      ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
      // ASSERT(bb2_with_args.args.size() == 0);
      generate_bb_args(bb2_with_args, res, data);
      res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
    }
    return true;
  });

  rules.add(pm::CondBranch, [](fir::Instr branch_instr, MatchResult &res,
                               ExtraMatchData &data) {
    fir::Instr cmp_instr;
    if (!pm::match(branch_instr,
                   pm::CondBranch(pm::capture(cmp_instr, pm::ICmp())))) {
      return false;
    }

    auto sub_type =
        static_cast<fir::ICmpInstrSubType>(cmp_instr->get_instr_subtype());

    // first we check if we can output a simplified version
    if (sub_type != fir::ICmpInstrSubType::SLT &&
        sub_type != fir::ICmpInstrSubType::SGE &&
        sub_type != fir::ICmpInstrSubType::SLE &&
        sub_type != fir::ICmpInstrSubType::SGT &&
        sub_type != fir::ICmpInstrSubType::NE &&
        sub_type != fir::ICmpInstrSubType::EQ &&
        sub_type != fir::ICmpInstrSubType::ULT &&
        sub_type != fir::ICmpInstrSubType::ULE &&
        sub_type != fir::ICmpInstrSubType::UGT &&
        sub_type != fir::ICmpInstrSubType::UGE) {
      fmt::println("Failed to smartly match cmp {} + branch\n",
                   cmp_instr->get_instr_subtype());
      return false;
    }

    auto &bb_with_args = branch_instr->bbs[0];
    auto target_bb = branch_instr->bbs[0].bb;
    auto v1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
    auto v2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

    // auto comp_ty = v1_orig.ty;

    // auto v1_reg = data.alloc.get_new_register(VRegInfo{comp_ty});
    // auto v2_reg = data.alloc.get_new_register(VRegInfo{comp_ty});
    // auto v1 = MArgument{v1_reg, comp_ty};
    // auto v2 = MArgument{v2_reg, comp_ty};

    // TODO: this is a issue with lifetimes
    // res.result.emplace_back(GBaseSubtype::mov, v1, v1_orig);
    // res.result.emplace_back(GBaseSubtype::mov, v2, v2_orig);

    ASSERT(bb_with_args.args.size() == target_bb->args.size());
    // ASSERT(bb_with_args.args.size() == 0);
    generate_bb_args(bb_with_args, res, data);

    if (sub_type == fir::ICmpInstrSubType::SLT) {
      res.result.push_back(MInstr::cJmp_slt(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::SGT) {
      res.result.push_back(MInstr::cJmp_sgt(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::ULT) {
      res.result.push_back(MInstr::cJmp_ult(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::ULE) {
      res.result.push_back(MInstr::cJmp_ule(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::UGT) {
      res.result.push_back(MInstr::cJmp_ugt(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::UGE) {
      res.result.push_back(MInstr::cJmp_uge(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::SGE) {
      res.result.push_back(MInstr::cJmp_sge(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::SLE) {
      res.result.push_back(MInstr::cJmp_sle(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::EQ) {
      res.result.push_back(MInstr::cJmp_eq(v1, v2, data.bbs[bb_with_args.bb]));
    } else if (sub_type == fir::ICmpInstrSubType::NE) {
      res.result.push_back(MInstr::cJmp_ne(v1, v2, data.bbs[bb_with_args.bb]));
    } else {
      UNREACH();
    }

    {
      auto bb2_with_args = branch_instr->bbs[1];
      auto target_bb2 = branch_instr->bbs[1].bb;
      ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
      // ASSERT(bb2_with_args.args.size() == 0);
      generate_bb_args(bb2_with_args, res, data);
      res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
    }
    return true;
  });
  rules.add(pm::CondBranch, [](fir::Instr branch_instr, MatchResult &res,
                               ExtraMatchData &data) {
    fir::Instr cmp_instr;
    if (!pm::match(branch_instr,
                   pm::CondBranch(pm::capture(cmp_instr, pm::FCmp())))) {
      return false;
    }

    auto sub_type =
        static_cast<fir::FCmpInstrSubType>(cmp_instr->get_instr_subtype());

    auto &bb_with_args = branch_instr->bbs[0];
    auto target_bb = branch_instr->bbs[0].bb;
    auto v1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
    auto v2 =
        valueToArgPosMem(cmp_instr->args[1], res.result, data.alloc, cmp_instr);

    ASSERT(bb_with_args.args.size() == target_bb->args.size());
    generate_bb_args(bb_with_args, res, data);

    // TODO: we can ofcourse support this
    if (sub_type == fir::FCmpInstrSubType::IsNaN) {
      return false;
    }

    res.result.push_back(
        MInstr::cJmp_flt(v1, v2, data.bbs[bb_with_args.bb], sub_type));

    {
      auto bb2_with_args = branch_instr->bbs[1];
      auto target_bb2 = branch_instr->bbs[1].bb;
      ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
      generate_bb_args(bb2_with_args, res, data);
      res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
    }
    return true;
  });
}

void arith_rules(RuleSet &rules) {
  rules.add(pm::SDiv,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              return generate_const_div(instr, true, false, res, data);
            });
  rules.add(pm::UDiv,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              return generate_const_div(instr, false, false, res, data);
            });
  rules.add(pm::SRem,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              return generate_const_div(instr, true, true, res, data);
            });
  rules.add(pm::URem,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              return generate_const_div(instr, false, true, res, data);
            });
  rules.add(pm::PtrAdd, [](fir::Instr add_instr, MatchResult &res,
                           ExtraMatchData &data) {
    fir::Instr mul_instr;
    i128 scale;
    if (!pm::match(
            add_instr,
            pm::PtrAdd(Reg, pm::capture(
                                mul_instr,
                                pm::IntMul(Reg, pm::const_int(
                                                    scale, is_addr_scale)))))) {
      return false;
    }

    // x + y * 1|2|4|8
    auto add_ty = convert_type(add_instr.result_type());
    if (add_ty >= Type::Float32) {
      // cant do it on vector regs
      return false;
    }
    auto res_ty = convert_type(add_instr.result_type());
    auto res_ty_size = get_size(res_ty);
    if (res_ty_size != 8 && res_ty_size != 4) {
      return false;
    }
    const i128 consti_val = addr_scale_shift(scale);

    // $1 = $0 * C
    // R = $2 + $1
    // where $0 and $2 must be regs and C in [1,2,4,8]
    auto res_reg = valueToArg(add_instr.result(), res.result, data.alloc);
    auto base = valueToArg(add_instr->args[0], res.result, data.alloc);
    auto indx = valueToArg(mul_instr->args[0], res.result, data.alloc);
    if (!base.isReg() || !indx.isReg()) {
      return false;
    }
    auto base_reg = base.reg;
    auto indx_reg = indx.reg;
    if (add_instr->args[0].is_constant_global()) {
      res.result.emplace_back(
          GBaseSubtype::mov, res_reg,
          MArgument::MemLIS(
              add_instr->args[0].as_constant()->as_global()->name.c_str(),
              indx_reg, consti_val, res_ty));
    }
    indx_reg.ty = res_ty;
    base_reg.ty = res_ty;
    res.result.emplace_back(
        X86Subtype::lea, res_reg,
        MArgument::MemBIS(base_reg, indx_reg, consti_val, res_ty));
    return true;
  });
  rules.add(pm::PtrAdd, [](fir::Instr add1_instr, MatchResult &res,
                           ExtraMatchData &data) {
    // TODO: handle x + c +y
    fir::Instr add0_instr;
    i128 consti_val;
    if (!pm::match(
            add1_instr,
            pm::PtrAdd(pm::capture(add0_instr,
                                   pm::PtrAdd(Reg, pm::const_int(consti_val,
                                                                 fits_disp32))),
                       Reg))) {
      return false;
    }

    // x + y + c
    // x + c + y
    auto add_ty = convert_type(add1_instr.result_type());
    if (add_ty >= Type::Float32) {
      // cant do it on vector regs
      return false;
    }
    auto res_ty = convert_type(add1_instr.result_type());
    auto res_ty_size = get_size(res_ty);
    if (res_ty_size != 8 && res_ty_size != 4) {
      return false;
    }
    auto res_reg = valueToArg(add1_instr.result(), res.result, data.alloc);
    auto a = valueToArg(add0_instr->args[0], res.result, data.alloc);
    auto b = valueToArg(add1_instr->args[1], res.result, data.alloc);
    if (!a.isReg() || !b.isReg()) {
      return false;
    }
    auto cpy_a = a.reg;
    auto cpy_b = b.reg;
    cpy_a.ty = res_ty;
    cpy_b.ty = res_ty;
    res.result.emplace_back(
        X86Subtype::lea, res_reg,
        MArgument::MemOBI(static_cast<u64>(static_cast<i64>(consti_val)), cpy_a,
                          cpy_b, res_ty));
    return true;
  });
  rules.add(pm::Fma, [](fir::Instr instr, MatchResult &res,
                        ExtraMatchData &data) {
    auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
    auto a = valueToArg(instr->args[0], res.result, data.alloc);
    auto b = valueToArg(instr->args[1], res.result, data.alloc);
    auto c = valueToArg(instr->args[2], res.result, data.alloc);
    // res = a * b + res
    res.result.emplace_back(GBaseSubtype::mov, res_reg, c);
    res.result.emplace_back(X86Subtype::ffmadd231, res_reg, a, b);
    return true;
  });
  rules.add(pm::IntMul, [](fir::Instr mul_instr, MatchResult &res,
                           ExtraMatchData &data) {
    // x*c where c is either 2 4 8  or 3 5 9
    auto res_ty = convert_type(mul_instr.result_type());
    auto res_ty_size = get_size(res_ty);
    if (res_ty_size != 8 && res_ty_size != 4) {
      return false;
    }
    i128 consti_val;
    if (!pm::match(mul_instr, pm::IntMul(Reg, pm::const_int(consti_val)))) {
      return false;
    }
    auto helper_reg = data.alloc.get_new_register(res_ty);
    auto res_reg = valueToArg(mul_instr.result(), res.result, data.alloc);

    auto base = valueToArg(mul_instr->args[0], res.result, data.alloc);
    ASSERT(base.isReg());
    return generate_lea_from_cmult(res_reg, helper_reg, base.reg, consti_val,
                                   res.result, res_ty);
  });
  rules.add(pm::IntMul, [](fir::Instr mul_instr, MatchResult &res,
                           ExtraMatchData &data) {
    // x*c where c is a power of 2
    auto res_ty = convert_type(mul_instr.result_type());
    auto res_ty_size = get_size(res_ty);
    if (res_ty_size > 8) {
      return false;
    }
    i128 consti_val;
    if (!pm::match(mul_instr, pm::IntMul(Reg, pm::const_int(consti_val)))) {
      return false;
    }
    if (consti_val <= 0) {
      return false;
    }

    // TODO: if off by 1 or similar could do shift and then use a add
    if (__builtin_popcountg(static_cast<u128>(consti_val)) != 1) {
      return false;
    }
    auto x = consti_val;
    u16 shift_width = 0;
    for (shift_width = 0; x != 1; x >>= 1, shift_width++) {
    }
    // auto  =
    //     sizeof(i128) * CHAR_BIT - __builtin_clzg((u128)consti_val) - 1;

    ASSERT(shift_width < 256);
    auto res_reg = valueToArg(mul_instr.result(), res.result, data.alloc);
    auto base = valueToArg(mul_instr->args[0], res.result, data.alloc);
    ASSERT(base.isReg());
    res.result.emplace_back(GBaseSubtype::mov, res_reg, base);
    res.result.emplace_back(GArithSubtype::shl2, res_reg,
                            MArgument(static_cast<u8>(shift_width)));
    return true;
  });

  rules.add(pm::FloatAdd, [](fir::Instr add_instr, MatchResult &res,
                             ExtraMatchData &data) {
    fir::Instr mul_instr;
    fir::ValueR addend;
    if (!pm::match(add_instr,
                   pm::commutative(pm::FloatAdd,
                                   pm::capture(mul_instr, pm::FloatMul()),
                                   pm::value(addend)))) {
      return false;
    }

    auto res_reg = valueToArg(add_instr.result(), res.result, data.alloc);
    auto mul_arg1 = valueToArg(mul_instr->args[0], res.result, data.alloc);
    auto mul_arg2 = valueToArg(mul_instr->args[1], res.result, data.alloc);
    auto add_arg2 = valueToArg(addend, res.result, data.alloc);

    // TODO: need a fpcontract or fastmath flag for this
    //  if (mul_arg1.isReg() && mul_arg2.isReg() && add_arg2.isReg()) {
    //    res.result.emplace_back(GBaseSubtype::mov, res_reg, mul_arg1);
    //    res.result.emplace_back(X86Subtype::ffmadd132, res_reg, add_arg2,
    //                            mul_arg2);
    //    fmt::println("{:cd}\n{:cd}", mul_instr, add_instr);
    //    fmt::println("{}", res.result);
    //  } else
    {
      res.result.emplace_back(GVecSubtype::vmul, res_reg, mul_arg1, mul_arg2);
      res.result.emplace_back(GVecSubtype::vadd, res_reg, res_reg, add_arg2);
    }
    return true;
  });
  rules.add(pm::IntMul, [](fir::Instr mul_instr, MatchResult &res,
                           ExtraMatchData &data) {
    // x*c where c can be split into powers of two that can be added or
    // substracted and this list must be short
    auto res_ty = convert_type(mul_instr.result_type());
    auto res_ty_size = get_size(res_ty);

    if (res_ty_size != 4) {
      return false;
    }
    res_ty = Type::Int64;
    res_ty_size = 8;
    i128 consti_val;
    if (!pm::match(mul_instr, pm::IntMul(Reg, pm::const_int(consti_val)))) {
      return false;
    }
    if (consti_val < 0 || __builtin_clzg(static_cast<u128>(consti_val)) <
                              ((static_cast<i32>(res_ty_size) * 8) + 1)) {
      //  TOOD: should be possible to extend to negative
      return false;
    }
    // fmt::println("{}", consti_val);
    constexpr u32 MAX_SIZE = 3;
    i128 multi_needed[MAX_SIZE + 1];
    u32 multi_index = 0;

    while (consti_val != 0) {
      bool negated = consti_val < 0;
      auto abs_consti_val = negated ? -consti_val : consti_val;
      if (abs_consti_val <= 16) {
        multi_needed[multi_index] = consti_val;
        multi_index++;
        consti_val = 0;
        continue;
      }

      u64 upper_pow =
          static_cast<u64>(utils::npow2(static_cast<u128>(abs_consti_val)));
      auto upper_pow_v = (static_cast<i128>(1)) << upper_pow;
      u64 lower_pow = upper_pow >> 1;
      auto lower_pow_v = (static_cast<i128>(1)) << lower_pow;
      auto selected_pow_v = lower_pow_v;
      auto selected_pow_i = lower_pow;
      if (std::abs(abs_consti_val - upper_pow_v) <=
          std::abs(abs_consti_val - lower_pow_v)) {
        selected_pow_v = upper_pow_v;
        selected_pow_i = upper_pow;
      }
      multi_needed[multi_index] =
          (negated ? -(static_cast<i128>(1) << selected_pow_i)
                   : static_cast<i128>(1) << selected_pow_i);

      multi_index++;
      consti_val =
          negated ? consti_val + selected_pow_v : consti_val - selected_pow_v;
      if (multi_index > MAX_SIZE) {
        return false;
      }
    }

    auto res_reg = valueToArg(mul_instr.result(), res.result, data.alloc);
    res_reg.ty = res_ty;
    res_reg.reg.ty = res_ty;
    auto base = valueToArg(mul_instr->args[0], res.result, data.alloc);
    base.ty = res_ty;
    base.reg.ty = res_ty;
    ASSERT(base.isReg());

    for (size_t i = 0; i < multi_index; i++) {
      auto curr_multi = multi_needed[i];
      bool negated = curr_multi < 0;
      auto abs_c_multi = negated ? -curr_multi : curr_multi;
      if (abs_c_multi >= 1 && abs_c_multi <= 16) {
        auto helper_reg = data.alloc.get_new_register(res_ty);
        if (i == 0) {
          ASSERT(generate_lea_from_cmult(res_reg, helper_reg, base.reg,
                                         abs_c_multi, res.result, res_ty));
        } else {
          auto res2_reg =
              MArgument(data.alloc.get_new_register(res_ty), res_ty);
          ASSERT(generate_lea_from_cmult(res2_reg, helper_reg, base.reg,
                                         abs_c_multi, res.result, res_ty));
          if (negated) {
            res.result.emplace_back(GArithSubtype::sub2, res_reg, res2_reg);
          } else {
            res.result.emplace_back(GArithSubtype::add2, res_reg, res2_reg);
          }
        }
      } else {
        auto helper_reg =
            MArgument(data.alloc.get_new_register(res_ty), res_ty);
        res.result.emplace_back(GBaseSubtype::mov, helper_reg, base);
        auto p2 = utils::npow2(abs_c_multi);
        res.result.emplace_back(GArithSubtype::shl2, helper_reg,
                                MArgument(static_cast<u64>(p2)));
        if (negated) {
          if (i == 0) {
            ASSERT(false);
          }
          res.result.emplace_back(GArithSubtype::sub2, res_reg, helper_reg);
        } else {
          if (i == 0) {
            res.result.emplace_back(GBaseSubtype::mov, res_reg, helper_reg);
          } else {
            res.result.emplace_back(GArithSubtype::add2, res_reg, helper_reg);
          }
        }
      }
    }

    // fmt::println("REs ");
    // for (auto &g : res.result) {
    //   fmt::println("{}", g);
    // }
    // TODO("okak");
    utils::StatCollector::get().addi(1, "mul_to_shift_chain",
                                     utils::StatCollector::StatMatcher);
    return true;
  });
}

// immediate shift amount of a vector shift when every lane shifts by the same
// constant
static std::optional<u8> splat_shift_amount(fir::ValueR v) {
  if (v.is_instr() && v.as_instr()->is(fir::VectorISubType::Broadcast)) {
    v = v.as_instr()->args[0];
  }
  if (v.is_constant() && v.as_constant()->is_int()) {
    return static_cast<u8>(v.as_constant()->as_int());
  }
  return std::nullopt;
}

// every lane shifts by the same value of a register, vpsllw and friends take
// the count from the low 64 bits of an xmm register
static bool word_shift_by_scalar(fir::Instr shift_instr, X86Subtype op,
                                 const MArgument &res_reg, const MArgument &a,
                                 MatchResult &res, ExtraMatchData &data) {
  const auto amount = shift_instr->args[1];
  if (!amount.is_instr() ||
      !amount.as_instr()->is(fir::VectorISubType::Broadcast)) {
    fmt::println("{}", shift_instr);
    TODO("word shift by a per lane amount");
  }
  auto scalar = valueToArg(amount.as_instr()->args[0], res.result, data.alloc);
  auto gpr = MArgument{data.alloc.get_new_register(Type::Int32), Type::Int32};
  if (scalar.isImm()) {
    res.result.emplace_back(GBaseSubtype::mov, gpr,
                            MArgument::Int(scalar.imm, Type::Int32));
  } else if (get_size(scalar.ty) < 4) {
    res.result.emplace_back(GConvSubtype::mov_zx, gpr, scalar);
  } else {
    res.result.emplace_back(
        GBaseSubtype::mov, gpr,
        MArgument{scalar.reg.retype(Type::Int32), Type::Int32});
  }
  auto xmm =
      MArgument{data.alloc.get_new_register(Type::Float32), Type::Float32};
  res.result.emplace_back(GBaseSubtype::mov, xmm, gpr);
  res.result.emplace_back(op, res_reg, a, xmm);
  return true;
}

void vec_rules(RuleSet &rules) {
  rules.add(pm::Broadcast, [](fir::Instr broad_instr, MatchResult &res,
                              ExtraMatchData &data) {
    fir::Instr n0;
    if (!pm::match(broad_instr, pm::Broadcast(pm::capture(n0, pm::Load())))) {
      return false;
    }

    auto res_reg = valueToArg(broad_instr.result(), res.result, data.alloc);
    auto arg = valueToArgPosMem(broad_instr->args[0], res.result, data.alloc,
                                broad_instr);
    if (arg.isReg() && !arg.is_fp()) {
      auto xmm_ty = arg.ty == Type::Int32 ? Type::Float32 : Type::Float64;
      auto helper = MArgument(data.alloc.get_new_register(xmm_ty), xmm_ty);
      res.result.emplace_back(GBaseSubtype::mov, helper, arg);
      arg = helper;
    }
    res.result.emplace_back(X86Subtype::vbroadcast, res_reg, arg);
    return true;
  });
}

void base_binary_rules(RuleSet &rules) {
  rules.add(pm::FloatAdd, [](fir::Instr f_add_instr, MatchResult &res,
                             ExtraMatchData &data) {
    auto a1 = valueToArg(f_add_instr->args[0], res.result, data.alloc);
    auto a2 = valueToArgPosMem(f_add_instr->args[1], res.result, data.alloc,
                               f_add_instr);
    auto res_reg = valueToArg(f_add_instr.result(), res.result, data.alloc);

    res.result.emplace_back(GVecSubtype::vadd, res_reg, a1, a2);
    return true;
  });
  rules.add(pm::FloatSub, [](fir::Instr f_sub_instr, MatchResult &res,
                             ExtraMatchData &data) {
    auto a1 = valueToArg(f_sub_instr->args[0], res.result, data.alloc);
    auto a2 = valueToArgPosMem(f_sub_instr->args[1], res.result, data.alloc,
                               f_sub_instr);
    auto res_reg = valueToArg(f_sub_instr.result(), res.result, data.alloc);

    res.result.emplace_back(GVecSubtype::vsub, res_reg, a1, a2);
    return true;
  });
  rules.add(pm::FloatMul, [](fir::Instr f_mul_instr, MatchResult &res,
                             ExtraMatchData &data) {
    auto a1 = valueToArg(f_mul_instr->args[0], res.result, data.alloc);
    auto a2 = valueToArgPosMem(f_mul_instr->args[1], res.result, data.alloc,
                               f_mul_instr);
    auto res_reg = valueToArg(f_mul_instr.result(), res.result, data.alloc);

    res.result.emplace_back(GVecSubtype::vmul, res_reg, a1, a2);
    return true;
  });
  rules.add(pm::FloatDiv, [](fir::Instr f_div_instr, MatchResult &res,
                             ExtraMatchData &data) {
    auto a1 = valueToArg(f_div_instr->args[0], res.result, data.alloc);
    auto a2 = valueToArgPosMem(f_div_instr->args[1], res.result, data.alloc,
                               f_div_instr);
    auto res_reg = valueToArg(f_div_instr.result(), res.result, data.alloc);

    res.result.emplace_back(GVecSubtype::vdiv, res_reg, a1, a2);
    return true;
  });
  auto i1_add_sub = [](fir::Instr instr, MatchResult &res,
                       ExtraMatchData &data) {
    if (!instr.result_type()->is_int() || instr.result_type()->as_int() != 1) {
      return false;
    }
    auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
    res.result.emplace_back(GBaseSubtype::mov, res_reg,
                            valueToArg(instr->args[0], res.result, data.alloc));
    res.result.emplace_back(GArithSubtype::lxor2, res_reg,
                            valueToArg(instr->args[1], res.result, data.alloc));
    return true;
  };
  rules.add(pm::IntAdd, i1_add_sub);
  rules.add(pm::IntSub, i1_add_sub);
  rules.add(pm::IntAdd, [](fir::Instr add_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(add_instr.result(), res.result, data.alloc);

    auto res_ty = convert_type(add_instr.result_type());
    if (res_ty >= Type::Float32) {
      // int vector add has different 3 oeprand operation
      auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);
      auto a1 = valueToArgPosMem(add_instr->args[1], res.result, data.alloc,
                                 add_instr);
      res.result.emplace_back(GVecSubtype::vadd, res_reg, a0, a1);
      return true;
    }

    auto a0 =
        valueToArgPosMem(add_instr->args[0], res.result, data.alloc, add_instr);
    if (res_reg.ty != a0.ty) {
      if (a0.isMem()) {
        a0.ty = res_reg.ty;
      } else {
        auto res_reg = data.alloc.get_new_register(res_ty);
        auto helper_reg0 = MArgument(res_reg, res_ty);

        res.result.emplace_back(GBaseSubtype::mov, helper_reg0, a0);
        a0 = helper_reg0;
      }
    }

    auto a1 =
        valueToArgPosMem(add_instr->args[1], res.result, data.alloc, add_instr);
    if (a1.isImm()) {
      // then we gucci
    } else if (res_reg.ty != a1.ty) {
      auto res_reg = data.alloc.get_new_register(res_ty);
      auto helper_reg1 = MArgument(res_reg, res_ty);

      res.result.emplace_back(GBaseSubtype::mov, helper_reg1, a1);
      a1 = helper_reg1;
    }

    res.result.emplace_back(GBaseSubtype::mov, res_reg, a0);
    res.result.emplace_back(GArithSubtype::add2, res_reg, a1);
    return true;
  });
  rules.add(pm::PtrAdd, [](fir::Instr add_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(add_instr.result(), res.result, data.alloc);

    auto res_ty = convert_type(add_instr.result_type());
    if (res_ty >= Type::Float32) {
      // int vector add has different 3 oeprand operation
      auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);
      auto a1 = valueToArgPosMem(add_instr->args[1], res.result, data.alloc,
                                 add_instr);
      res.result.emplace_back(GVecSubtype::vadd, res_reg, a0, a1);
      return true;
    }

    auto a0 =
        valueToArgPosMem(add_instr->args[0], res.result, data.alloc, add_instr);
    if (res_reg.ty != a0.ty) {
      if (a0.isMem()) {
        a0.ty = res_reg.ty;
      } else {
        auto res_reg = data.alloc.get_new_register(res_ty);
        auto helper_reg0 = MArgument(res_reg, res_ty);

        res.result.emplace_back(GBaseSubtype::mov, helper_reg0, a0);
        a0 = helper_reg0;
      }
    }

    auto a1 =
        valueToArgPosMem(add_instr->args[1], res.result, data.alloc, add_instr);
    if (a1.isImm()) {
      // then we gucci
    } else if (res_reg.ty != a1.ty) {
      auto res_reg = data.alloc.get_new_register(res_ty);
      auto helper_reg1 = MArgument(res_reg, res_ty);

      res.result.emplace_back(GBaseSubtype::mov, helper_reg1, a1);
      a1 = helper_reg1;
    }

    res.result.emplace_back(GBaseSubtype::mov, res_reg, a0);
    res.result.emplace_back(GArithSubtype::add2, res_reg, a1);
    return true;
  });

  rules.add(pm::IntSub, [](fir::Instr sub_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(sub_instr.result(), res.result, data.alloc);
    auto res_ty = convert_type(sub_instr.result_type());
    if (res_ty >= Type::Float32) {
      // int vector add has different 3 oeprand operation
      auto a0 = valueToArg(sub_instr->args[0], res.result, data.alloc);
      auto a1 = valueToArg(sub_instr->args[1], res.result, data.alloc);
      res.result.emplace_back(GVecSubtype::vsub, res_reg, a0, a1);
      return true;
    }
    auto a0 = valueToArg(sub_instr->args[0], res.result, data.alloc);
    if (a0.isImm()) {
      // then we gucci
    } else if (res_reg.ty != a0.ty) {
      auto res_reg = data.alloc.get_new_register(res_ty);
      auto helper_reg1 = MArgument(res_reg, res_ty);
      res.result.emplace_back(GBaseSubtype::mov, helper_reg1, a0);
      a0 = helper_reg1;
    }

    auto a1 = valueToArg(sub_instr->args[1], res.result, data.alloc);
    if (a1.isImm()) {
      // then we gucci
    } else if (res_reg.ty != a1.ty) {
      auto res_reg = data.alloc.get_new_register(res_ty);
      auto helper_reg1 = MArgument(res_reg, res_ty);
      res.result.emplace_back(GBaseSubtype::mov, helper_reg1, a1);
      a1 = helper_reg1;
    }

    res.result.emplace_back(GBaseSubtype::mov, res_reg, a0);
    res.result.emplace_back(GArithSubtype::sub2, res_reg, a1);
    return true;
  });
  rules.add(pm::Shl, [](fir::Instr shift_instr, MatchResult &res,
                        ExtraMatchData &data) {
    auto res_reg = valueToArg(shift_instr.result(), res.result, data.alloc);

    auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
    auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);
    if (res_reg.is_vec_reg()) {
      switch (shift_instr->result_type()->as_vec().bitwidth) {
        case 16: {
          const auto amount = splat_shift_amount(shift_instr->args[1]);
          if (amount) {
            res.result.emplace_back(X86Subtype::psll, res_reg, a,
                                    MArgument(*amount));
            return true;
          }
          return word_shift_by_scalar(shift_instr, X86Subtype::psll, res_reg, a,
                                      res, data);
        }
        case 32:
        case 64:
          // vpsllvd ymm0, ymm1, ymm2
          res.result.emplace_back(GVecSubtype::vShl, res_reg, a, b);
          return true;
          // idk this seems not very easy for avx2
        default:
          fmt::println("{}", shift_instr);
          TODO("impl");
      }
    }

    if (b.isImm()) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg, a);
      res.result.emplace_back(GArithSubtype::shl2, res_reg, b);
    } else {
      auto shift_reg = VReg::CL();
      auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
      if (b.ty == Type::Int8) {
        res.result.emplace_back(GBaseSubtype::mov, shift_reg_arg, b);
      } else {
        res.result.emplace_back(GConvSubtype::itrunc, shift_reg_arg, b);
      }
      res.result.emplace_back(GBaseSubtype::mov, res_reg, a);
      res.result.emplace_back(GArithSubtype::shl2, res_reg, shift_reg_arg);
    }
    return true;
  });
  rules.add(pm::Shr, [](fir::Instr shift_instr, MatchResult &res,
                        ExtraMatchData &data) {
    auto res_reg = valueToArg(shift_instr.result(), res.result, data.alloc);

    auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
    auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);

    if (res_reg.is_vec_reg()) {
      switch (shift_instr->result_type()->as_vec().bitwidth) {
        case 16: {
          const auto amount = splat_shift_amount(shift_instr->args[1]);
          if (amount) {
            res.result.emplace_back(X86Subtype::psrl, res_reg, a,
                                    MArgument(*amount));
            return true;
          }
          return word_shift_by_scalar(shift_instr, X86Subtype::psrl, res_reg, a,
                                      res, data);
        }
        case 32:
        case 64:
          res.result.emplace_back(GVecSubtype::vShr, res_reg, a, b);
          return true;
        default:
          fmt::println("{}", shift_instr);
          TODO("impl");
      }
    }

    if (b.isImm()) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg, a);
      res.result.emplace_back(GArithSubtype::shr2, res_reg, b);
    } else {
      auto shift_reg = VReg::CL();
      auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
      if (b.ty == Type::Int8) {
        res.result.emplace_back(GBaseSubtype::mov, shift_reg_arg, b);
      } else {
        res.result.emplace_back(GConvSubtype::itrunc, shift_reg_arg, b);
      }
      res.result.emplace_back(GBaseSubtype::mov, res_reg, a);
      res.result.emplace_back(GArithSubtype::shr2, res_reg, shift_reg_arg);
    }
    return true;
  });
  rules.add(pm::AShr, [](fir::Instr shift_instr, MatchResult &res,
                         ExtraMatchData &data) {
    auto res_reg = valueToArg(shift_instr.result(), res.result, data.alloc);

    auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
    auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);

    if (res_reg.is_vec_reg()) {
      switch (shift_instr->result_type()->as_vec().bitwidth) {
        case 16: {
          const auto amount = splat_shift_amount(shift_instr->args[1]);
          if (amount) {
            res.result.emplace_back(X86Subtype::psra, res_reg, a,
                                    MArgument(*amount));
            return true;
          }
          return word_shift_by_scalar(shift_instr, X86Subtype::psra, res_reg, a,
                                      res, data);
        }
        case 32:
          res.result.emplace_back(GVecSubtype::vSar, res_reg, a, b);
          return true;
        case 64: {
          // no 64 bit lane arithmetic shift before avx512: for negative
          // lanes shift the complement instead, ~((~x) >>l n)
          auto ty = res_reg.ty;
          auto fresh = [&] {
            return MArgument(data.alloc.get_new_register(ty), ty);
          };
          auto ones = fresh();
          auto not_a = fresh();
          auto lo = fresh();
          auto lo_neg = fresh();
          auto hi_neg = fresh();
          res.result.emplace_back(X86Subtype::vpcmpeq, ones, ones, ones);
          res.result.emplace_back(GVecSubtype::vXor, not_a, a, ones);
          res.result.emplace_back(GVecSubtype::vShr, lo, a, b);
          res.result.emplace_back(GVecSubtype::vShr, lo_neg, not_a, b);
          res.result.emplace_back(GVecSubtype::vXor, hi_neg, lo_neg, ones);
          res.result.emplace_back(X86Subtype::vblendv, res_reg, lo, hi_neg, a);
          return true;
        }
        default:
          fmt::println("{}", shift_instr);
          TODO("impl");
      }
    }

    if (b.isImm()) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg, a);
      res.result.emplace_back(GArithSubtype::sar2, res_reg, b);
    } else {
      auto shift_reg = VReg::CL();
      auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
      if (b.ty == Type::Int8) {
        res.result.emplace_back(GBaseSubtype::mov, shift_reg_arg, b);
      } else {
        res.result.emplace_back(GConvSubtype::itrunc, shift_reg_arg, b);
      }
      res.result.emplace_back(GBaseSubtype::mov, res_reg, a);
      res.result.emplace_back(GArithSubtype::sar2, res_reg, shift_reg_arg);
    }
    return true;
  });
  rules.add(pm::IntMul, [](fir::Instr add_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(add_instr.result(), res.result, data.alloc);
    if (res_reg.ty == Type::Int8) {
      // cant do mul2 on a int8 atleast not as easy
      // since there is only a version which multiplies by al
      // TODO: prob should impl that
      res.result.emplace_back(
          GBaseSubtype::mov, res_reg,
          valueToArg(add_instr->args[0], res.result, data.alloc));
      auto res_reg16 = res_reg;
      ASSERT(res_reg16.isReg());
      res_reg16.ty = Type::Int16;
      res_reg16.reg.ty = Type::Int16;
      auto arg1 = valueToArgPosMem(add_instr->args[1], res.result, data.alloc,
                                   add_instr);
      if (arg1.isReg()) {
        arg1.ty = Type::Int16;
        arg1.reg.ty = Type::Int16;
      } else if (arg1.isImm() || arg1.isMem()) {
        arg1.ty = Type::Int16;
      } else {
        ASSERT(false);
      }
      // only the low byte of the product is the result, the rest of the
      // register is free like for every other i8 value
      res.result.emplace_back(GArithSubtype::mul2, res_reg16, arg1);
      return true;
    }

    if (res_reg.is_vec_reg()) {
      res.result.emplace_back(
          GVecSubtype::vmul, res_reg,
          valueToArg(add_instr->args[0], res.result, data.alloc),
          valueToArgPosMem(add_instr->args[1], res.result, data.alloc,
                           add_instr));
    } else {
      res.result.emplace_back(GBaseSubtype::mov, res_reg,
                              valueToArgPosMem(add_instr->args[0], res.result,
                                               data.alloc, add_instr));
      res.result.emplace_back(GArithSubtype::mul2, res_reg,
                              valueToArgPosMem(add_instr->args[1], res.result,
                                               data.alloc, add_instr));
    }
    return true;
  });
  rules.add(
      pm::Or, [](fir::Instr or_instr, MatchResult &res, ExtraMatchData &data) {
        auto res_reg = valueToArg(or_instr.result(), res.result, data.alloc);

        if (res_reg.is_vec_reg()) {
          res.result.emplace_back(
              GVecSubtype::vOr, res_reg,
              valueToArg(or_instr->args[0], res.result, data.alloc),
              valueToArg(or_instr->args[1], res.result, data.alloc));
        } else {
          res.result.emplace_back(
              GBaseSubtype::mov, res_reg,
              valueToArg(or_instr->args[0], res.result, data.alloc));
          res.result.emplace_back(
              GArithSubtype::lor2, res_reg,
              valueToArg(or_instr->args[1], res.result, data.alloc));
        }
        return true;
      });
  rules.add(pm::And, [](fir::Instr and_instr, MatchResult &res,
                        ExtraMatchData &data) {
    auto res_reg = valueToArg(and_instr.result(), res.result, data.alloc);

    if (res_reg.is_vec_reg()) {
      res.result.emplace_back(
          GVecSubtype::vAnd, res_reg,
          valueToArg(and_instr->args[0], res.result, data.alloc),
          valueToArg(and_instr->args[1], res.result, data.alloc));
    } else {
      res.result.emplace_back(
          GBaseSubtype::mov, res_reg,
          valueToArg(and_instr->args[0], res.result, data.alloc));
      res.result.emplace_back(
          GArithSubtype::land2, res_reg,

          valueToArg(and_instr->args[1], res.result, data.alloc));
    }
    return true;
  });
  rules.add(pm::Xor, [](fir::Instr xor_instr, MatchResult &res,
                        ExtraMatchData &data) {
    auto res_reg = valueToArg(xor_instr.result(), res.result, data.alloc);

    if (res_reg.is_vec_reg()) {
      res.result.emplace_back(
          GVecSubtype::vXor, res_reg,
          valueToArg(xor_instr->args[0], res.result, data.alloc),
          valueToArg(xor_instr->args[1], res.result, data.alloc));
    } else {
      res.result.emplace_back(
          GBaseSubtype::mov, res_reg,
          valueToArg(xor_instr->args[0], res.result, data.alloc));
      res.result.emplace_back(
          GArithSubtype::lxor2, res_reg,
          valueToArg(xor_instr->args[1], res.result, data.alloc));
    }
    return true;
  });
  rules.add(pm::SRem, [](fir::Instr srem_instr, MatchResult &res,
                         ExtraMatchData &data) {
    // FIXME: variable size
    auto res_div = VReg::EAX();
    auto res_rem = VReg::EDX();
    auto res_reg = valueToArg(srem_instr.result(), res.result, data.alloc);
    auto res_div_arg =
        MArgument(res_div, convert_type(srem_instr.result_type()));
    auto res_rem_arg =
        MArgument(res_rem, convert_type(srem_instr.result_type()));

    res.result.emplace_back(
        GArithSubtype::idiv, res_div_arg, res_rem_arg,
        valueToArg(srem_instr->args[0], res.result, data.alloc),
        valueToArg(srem_instr->args[1], res.result, data.alloc));
    res.result.emplace_back(GBaseSubtype::mov, res_reg, res_rem_arg);
    return true;
  });
  rules.add(pm::URem, [](fir::Instr srem_instr, MatchResult &res,
                         ExtraMatchData &data) {
    // FIXME: variable size
    auto res_div = VReg::EAX();
    auto res_rem = VReg::EDX();
    auto res_reg = valueToArg(srem_instr.result(), res.result, data.alloc);
    auto res_div_arg =
        MArgument(res_div, convert_type(srem_instr.result_type()));
    auto res_rem_arg =
        MArgument(res_rem, convert_type(srem_instr.result_type()));

    res.result.emplace_back(
        GArithSubtype::udiv, res_div_arg, res_rem_arg,
        valueToArg(srem_instr->args[0], res.result, data.alloc),
        valueToArg(srem_instr->args[1], res.result, data.alloc));
    res.result.emplace_back(GBaseSubtype::mov, res_reg, res_rem_arg);
    return true;
  });
  rules.add(pm::SDiv, [](fir::Instr sdiv_instr, MatchResult &res,
                         ExtraMatchData &data) {
    // FIXME: variable size
    auto res_div = VReg::EAX();
    auto res_rem = VReg::EDX();
    auto res_reg = valueToArg(sdiv_instr.result(), res.result, data.alloc);
    auto res_div_arg =
        MArgument(res_div, convert_type(sdiv_instr.result_type()));
    auto res_rem_arg =
        MArgument(res_rem, convert_type(sdiv_instr.result_type()));

    res.result.emplace_back(
        GArithSubtype::idiv, res_div_arg, res_rem_arg,
        valueToArg(sdiv_instr->args[0], res.result, data.alloc),
        valueToArg(sdiv_instr->args[1], res.result, data.alloc));
    res.result.emplace_back(GBaseSubtype::mov, res_reg, res_div_arg);
    return true;
  });
  rules.add(pm::UDiv, [](fir::Instr sdiv_instr, MatchResult &res,
                         ExtraMatchData &data) {
    // FIXME: variable size
    auto res_div = VReg::EAX();
    auto res_rem = VReg::EDX();
    auto res_reg = valueToArg(sdiv_instr.result(), res.result, data.alloc);
    auto res_div_arg =
        MArgument(res_div, convert_type(sdiv_instr.result_type()));
    auto res_rem_arg =
        MArgument(res_rem, convert_type(sdiv_instr.result_type()));

    res.result.emplace_back(
        GArithSubtype::udiv, res_div_arg, res_rem_arg,
        valueToArg(sdiv_instr->args[0], res.result, data.alloc),
        valueToArg(sdiv_instr->args[1], res.result, data.alloc));
    res.result.emplace_back(GBaseSubtype::mov, res_reg, res_div_arg);
    return true;
  });
}

void base_rules(RuleSet &rules) {
  base_binary_rules(rules);

  rules.add(pm::Fence, [](fir::Instr fence_instr, MatchResult &res,
                          ExtraMatchData &) {
    // x86 TSO only reorders a store with a later load, which only a seq_cst
    // fence forbids
    if (static_cast<fir::Ordering>(fence_instr->Ordering) ==
        fir::Ordering::Seq_Cst) {
      res.result.emplace_back(X86Subtype::mfence);
    }
    return true;
  });

  rules.add(pm::AtomicRMW, [](fir::Instr atomic_instr, MatchResult &res,
                              ExtraMatchData &data) {
    // every locked instruction on x86 is seq_cst, so the ordering is not used
    using Sub = fir::AtomicRMWSubType;
    auto op = static_cast<Sub>(atomic_instr->subtype);
    auto ty = convert_type(atomic_instr.result_type());
    bool used = atomic_instr.result().get_n_uses() != 0;
    if (!used && (op == Sub::Max || op == Sub::Min || op == Sub::UMax ||
                  op == Sub::UMin || op == Sub::Nand)) {
      TODO(
          "atomicrmw without a single instruction has to be expanded by "
          "IntrinExpand");
    }
    if (used && (op == Sub::And || op == Sub::Or || op == Sub::Xor)) {
      TODO("atomicrmw with a used result has to be expanded by IntrinExpand");
    }

    auto res_reg = valueToArg(atomic_instr.result(), res.result, data.alloc);
    auto ptr = valueToArg(atomic_instr->args[0], res.result, data.alloc);
    if (!ptr.isReg()) {
      auto ptr_reg =
          MArgument{data.alloc.get_new_register(Type::Int64), Type::Int64};
      res.result.emplace_back(GBaseSubtype::mov, ptr_reg, ptr);
      ptr = ptr_reg;
    }
    auto mem = MArgument::MemB(ptr.reg, ty);

    auto in_reg = [&](MArgument arg) {
      if (arg.isReg()) {
        return arg;
      }
      auto reg = MArgument{data.alloc.get_new_register(ty), ty};
      res.result.emplace_back(GBaseSubtype::mov, reg, arg);
      return reg;
    };
    // immediate forms exist for 32 bit and 64 bit as sign extended 32 bit
    auto operand = [&](MArgument arg) {
      if (arg.isImm() && (ty == Type::Int32 || ty == Type::Int64) &&
          std::bit_cast<i64>(arg.imm) == static_cast<i32>(arg.imm)) {
        return arg;
      }
      return in_reg(arg);
    };

    if (op == Sub::CmpXchg) {
      // expected goes in and the old value comes out of rax
      auto rax = MArgument(VReg::EAX(), ty);
      auto expected = valueToArg(atomic_instr->args[1], res.result, data.alloc);
      auto new_val =
          in_reg(valueToArg(atomic_instr->args[2], res.result, data.alloc));
      res.result.emplace_back(GBaseSubtype::mov, rax, expected);
      res.result.emplace_back(X86Subtype::LockCmpXchg3, mem, new_val, rax);
      res.result.emplace_back(GBaseSubtype::mov, res_reg, rax);
      return true;
    }

    auto val = valueToArg(atomic_instr->args[1], res.result, data.alloc);
    switch (op) {
      case Sub::INVALID:
      case Sub::CmpXchg:
        UNREACH();
      case Sub::Add:
        if (!used) {
          res.result.emplace_back(X86Subtype::LockAdd2, mem, operand(val));
          return true;
        }
        res.result.emplace_back(GBaseSubtype::mov, res_reg, val);
        res.result.emplace_back(X86Subtype::LockXAdd2, mem, res_reg);
        return true;
      case Sub::Sub:
        if (!used) {
          res.result.emplace_back(X86Subtype::LockSub2, mem, operand(val));
          return true;
        }
        // old = xadd(-val)
        res.result.emplace_back(GBaseSubtype::mov, res_reg, val);
        res.result.emplace_back(GArithSubtype::neg1, res_reg);
        res.result.emplace_back(X86Subtype::LockXAdd2, mem, res_reg);
        return true;
      case Sub::Xchg:
        res.result.emplace_back(GBaseSubtype::mov, res_reg, val);
        res.result.emplace_back(X86Subtype::Xchg2, mem, res_reg);
        return true;
      case Sub::And:
        res.result.emplace_back(X86Subtype::LockAnd2, mem, operand(val));
        return true;
      case Sub::Or:
        res.result.emplace_back(X86Subtype::LockOr2, mem, operand(val));
        return true;
      case Sub::Xor:
        res.result.emplace_back(X86Subtype::LockXor2, mem, operand(val));
        return true;
      case Sub::Nand:
      case Sub::Max:
      case Sub::Min:
      case Sub::UMax:
      case Sub::UMin:
        UNREACH();
    }
    UNREACH();
  });

  rules.add(pm::Alloca, [](fir::Instr alloca_instr, MatchResult &res,
                           ExtraMatchData &data) {
    // TODO: this should be done once for all allocas that only
    // get executed once
    auto rsp_reg = VReg::RSP();
    auto rsp_arg = MArgument{rsp_reg, Type::Int64};

    auto res_reg = valueToArg(alloca_instr.result(), res.result, data.alloc);
    if (!alloca_instr->args[0].is_constant()) {
      auto size_arg = valueToArg(alloca_instr->args[0], res.result, data.alloc);
      ASSERT_M(get_size(size_arg.ty) == 8, "dynamic alloca size is not 64 bit");
      auto size_reg = data.alloc.get_new_register(Type::Int64);
      auto tmp = MArgument{size_reg, Type::Int64};
      res.result.emplace_back(GBaseSubtype::mov, tmp, size_arg);
      res.result.emplace_back(GArithSubtype::add2, tmp,
                              MArgument(static_cast<u64>(15)));
      res.result.emplace_back(GArithSubtype::shr2, tmp,
                              MArgument(static_cast<u8>(4)));
      res.result.emplace_back(GArithSubtype::shl2, tmp,
                              MArgument(static_cast<u8>(4)));
      res.result.emplace_back(GArithSubtype::sub2, rsp_arg, tmp);
      res.result.emplace_back(GBaseSubtype::mov, res_reg, rsp_arg);
      return true;
    }

    auto orig_size =
        static_cast<u64>(alloca_instr->args[0].as_constant()->as_int());
    auto size = orig_size;
    // TODO: this is inefficient for many stack allocations
    // TODO: this also depends on the calling conv
    if (size % 16 != 0) {
      size = size + (16 - (size % 16));
    }

    res.result.emplace_back(GArithSubtype::sub2, rsp_arg, size);
    // if (data.bb_id == 0 && data.static_alloca_size == orig_size) {
    //   res.result.emplace_back(
    //       X86Subtype::lea, res_reg,
    //       //+8 for pushing the rbp
    //       MArgument::MemOB(-(i32)(size + 8), VReg::RBP(),
    //       Type::Int64));
    // } else {
    res.result.emplace_back(GBaseSubtype::mov, res_reg, rsp_arg);
    // }

    return true;
  });
  rules.add(pm::Load, [](fir::Instr load_instr, MatchResult &res,
                         ExtraMatchData &data) {
    auto res_reg = valueToArg(load_instr.result(), res.result, data.alloc);
    if (load_instr->args[0].get_type()->is_vec()) {
      auto base_reg = data.alloc.get_new_register(Type::Int64);
      auto base_arg = MArgument{base_reg, Type::Int64};
      auto mask_reg = data.alloc.get_new_register(res_reg.ty);
      auto mask = MArgument{mask_reg, res_reg.ty};
      auto arg = valueToArg(load_instr->args[0], res.result, data.alloc);
      ASSERT(arg.isReg());
      res.result.emplace_back(GArithSubtype::lxor2, base_arg, base_arg);
      res.result.emplace_back(X86Subtype::vpcmpeq, mask, mask, mask);
      res.result.emplace_back(X86Subtype::vgatherq, res_reg,
                              MArgument::MemOIS(0, arg.reg, 0, Type::Int32x4),
                              mask);
      return true;
    }
    auto arg = valueToArgPtr(load_instr->args[0],
                             convert_type(load_instr.result_type()), res.result,
                             data.alloc);
    arg.ty = convert_type(load_instr.result_type());
    res.result.emplace_back(GBaseSubtype::mov, res_reg, arg);
    return true;
  });
  rules.add(pm::Store, [](fir::Instr store_instr, MatchResult &res,
                          ExtraMatchData &data) {
    auto store_ty = store_instr.result_type();

    auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
    auto ptr_target = valueToArgPtr(
        store_instr->args[0], convert_type(store_instr->args[0].get_type()),
        res.result, data.alloc);

    ptr_target.ty = convert_type(store_ty);
    res.result.emplace_back(GBaseSubtype::mov, ptr_target, value);
    return true;
  });
  rules.add(pm::Select, [](fir::Instr select_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(select_instr.result(), res.result, data.alloc);
    auto cond = valueToArg(select_instr->args[0], res.result, data.alloc);
    auto a = valueToArg(select_instr->args[1], res.result, data.alloc);
    auto b = valueToArg(select_instr->args[2], res.result, data.alloc);

    if (res_reg.is_vec_reg() && !cond.is_vec_reg()) {
      // vector select with a scalar condition: build an all ones / all
      // zeros mask, splat it and blend
      auto wide =
          MArgument(data.alloc.get_new_register(Type::Int64), Type::Int64);
      res.result.emplace_back(GConvSubtype::mov_zx, wide, cond);
      res.result.emplace_back(GArithSubtype::neg1, wide);
      auto mask =
          MArgument(data.alloc.get_new_register(res_reg.ty), res_reg.ty);
      auto narrow =
          MArgument(data.alloc.get_new_register(Type::Float64), Type::Float64);
      res.result.emplace_back(GBaseSubtype::mov, narrow, wide);
      if (res_reg.ty == Type::Float32 || res_reg.ty == Type::Float64) {
        // for scalar float the all ones / zeros mask already is the value
        res.result.emplace_back(X86Subtype::vblendv, res_reg, b, a, narrow);
        return true;
      }
      res.result.emplace_back(X86Subtype::vbroadcast, mask, narrow);
      res.result.emplace_back(X86Subtype::vblendv, res_reg, b, a, mask);
      return true;
    }
    if (res_reg.is_vec_reg()) {
      // blendv takes the second source where the mask is set
      res.result.emplace_back(X86Subtype::vblendv, res_reg, b, a, cond);
      return true;
    }
    res.result.emplace_back(GBaseSubtype::mov, res_reg, b);
    res.result.emplace_back(GCMovSubtype::cmov, res_reg, cond, a);
    return true;
  });
  rules.add(pm::Not, [](fir::Instr not_instr, MatchResult &res,
                        ExtraMatchData &data) {
    auto res_reg = valueToArg(not_instr.result(), res.result, data.alloc);

    if (res_reg.is_fp()) {
      res.result.emplace_back(X86Subtype::vpcmpeq, res_reg, res_reg, res_reg);
      res.result.emplace_back(
          GVecSubtype::vXor, res_reg, res_reg,
          valueToArg(not_instr->args[0], res.result, data.alloc));
    } else if (res_reg.is_vec_reg()) {
      vec_not(res_reg, valueToArg(not_instr->args[0], res.result, data.alloc),
              res, data);
    } else {
      auto bitwidth = not_instr.result_type()->as_int();
      if (bitwidth == 1) {
        res.result.emplace_back(
            GBaseSubtype::mov, res_reg,
            valueToArg(not_instr->args[0], res.result, data.alloc));
        res.result.emplace_back(GArithSubtype::lxor2, res_reg,
                                MArgument(static_cast<u8>(0x1)));
        return true;
      }

      res.result.emplace_back(
          GBaseSubtype::mov, res_reg,
          valueToArg(not_instr->args[0], res.result, data.alloc));
      res.result.emplace_back(GArithSubtype::not1, res_reg);
    }
    return true;
  });
  rules.add(pm::FloatSqrt, [](fir::Instr sqrt_instr, MatchResult &res,
                              ExtraMatchData &data) {
    auto res_reg = valueToArg(sqrt_instr.result(), res.result, data.alloc);

    ASSERT(res_reg.is_fp() || res_reg.is_vec_reg());
    res.result.emplace_back(
        X86Subtype::sqrt, res_reg,
        valueToArg(sqrt_instr->args[0], res.result, data.alloc));
    return true;
  });
  static constexpr auto lower_hmul = [](fir::Instr hred_instr, MatchResult &res,
                                        ExtraMatchData &data) {
    auto res_reg = valueToArg(hred_instr.result(), res.result, data.alloc);
    auto a1 = valueToArg(hred_instr->args[0], res.result, data.alloc);
    (void)res_reg;

    switch (a1.ty) {
      case Type::INVALID:
      case Type::Int8:
      case Type::Int16:
      case Type::Int32:
      case Type::Int64:
      case Type::Float32:
      case Type::Float64:
        TODO("invalid?");
      case Type::Float32x2:
        res.result.emplace_back(X86Subtype::vmovshdup, res_reg, a1);
        res.result.emplace_back(GVecSubtype::vmul, res_reg, res_reg, a1);
        break;
      case Type::Float64x2:
        res.result.emplace_back(X86Subtype::vpermil, res_reg, a1,
                                MArgument::Int(1, Type::Int8));
        res.result.emplace_back(GVecSubtype::vmul, res_reg, res_reg, a1);
        break;
      default:
        fmt::println("{:cd}", hred_instr->args[0].get_type());
        fmt::println("{:cd}", hred_instr);
        TODO("impl");
        break;
    }
    return true;
  };
  static constexpr auto lower_hadd = [](fir::Instr hred_instr, MatchResult &res,
                                        ExtraMatchData &data) {
    auto res_reg = valueToArg(hred_instr.result(), res.result, data.alloc);
    auto a1 = valueToArg(hred_instr->args[0], res.result, data.alloc);

    switch (a1.ty) {
      default:
        TODO("invalid?");
      case Type::Float32x2:
        res.result.emplace_back(X86Subtype::vmovshdup, res_reg, a1);
        res.result.emplace_back(GVecSubtype::vadd, res_reg, res_reg, a1);
        break;
      case Type::Float32x4: {
        auto res_reg_exp = MArgument{res_reg.reg, Type::Float32x2};
        res.result.emplace_back(X86Subtype::HAdd, res_reg_exp, a1);
        res.result.emplace_back(X86Subtype::HAdd, res_reg, res_reg_exp);
        break;
      }
      case Type::Float32x8: {
        auto res_128 =
            MArgument{res_reg.reg.retype(Type::Float32x4), Type::Float32x4};
        auto a1_128 =
            MArgument{a1.reg.retype(Type::Float32x4), Type::Float32x4};
        auto res_64 =
            MArgument{res_reg.reg.retype(Type::Float32x2), Type::Float32x2};

        res.result.emplace_back(X86Subtype::vextract128, res_128, a1,
                                MArgument(static_cast<u8>(1)));
        res.result.emplace_back(GVecSubtype::vadd, res_128, res_128, a1_128);
        res.result.emplace_back(X86Subtype::HAdd, res_64, res_128);
        res.result.emplace_back(X86Subtype::HAdd, res_reg, res_64);
        break;
      }
      case Type::Float64x2: {
        res.result.emplace_back(X86Subtype::HAdd, res_reg, a1);
        break;
      }
      case Type::Float64x4: {
        auto res_128 =
            MArgument{res_reg.reg.retype(Type::Float64x2), Type::Float64x2};
        auto a1_128 =
            MArgument{a1.reg.retype(Type::Float64x2), Type::Float64x2};

        res.result.emplace_back(X86Subtype::vextract128, res_128, a1,
                                MArgument(static_cast<u8>(1)));
        res.result.emplace_back(GVecSubtype::vadd, res_128, res_128, a1_128);
        res.result.emplace_back(X86Subtype::HAdd, res_reg, res_128);
        break;
      }
      case Type::Float64x8: {
        auto res_256 =
            MArgument{res_reg.reg.retype(Type::Float64x4), Type::Float64x4};
        auto a1_256 =
            MArgument{a1.reg.retype(Type::Float64x4), Type::Float64x4};
        auto res_128 =
            MArgument{res_reg.reg.retype(Type::Float64x2), Type::Float64x2};

        // 256 red
        res.result.emplace_back(X86Subtype::vextractf64x4, res_256, a1,
                                MArgument(static_cast<u8>(1)));
        res.result.emplace_back(GVecSubtype::vadd, res_256, res_256, a1_256);

        // 128 red
        auto res_256_low_128 = MArgument{res_reg.reg, Type::Float64x2};
        res.result.emplace_back(X86Subtype::vextract128, res_128, res_256,
                                MArgument(static_cast<u8>(1)));
        res.result.emplace_back(GVecSubtype::vadd, res_128, res_128,
                                res_256_low_128);

        res.result.emplace_back(X86Subtype::HAdd, res_reg, res_128);
        break;
      }
      case Type::Int64x2: {
        auto helper = data.alloc.get_new_register(Type::Int64);
        auto helper_arg = MArgument{helper, Type::Int64};
        res.result.emplace_back(GBaseSubtype::mov, helper_arg, a1);
        res.result.emplace_back(X86Subtype::vpextr, res_reg, a1,
                                MArgument(static_cast<u8>(1)));
        res.result.emplace_back(GArithSubtype::add2, res_reg, helper_arg);
        break;
      }
      case Type::Int64x4: {
        auto helper_vec = data.alloc.get_new_register(Type::Int64x2);
        auto helper_vec_arg = MArgument{helper_vec, Type::Int64x2};

        auto helper = data.alloc.get_new_register(Type::Int64);
        auto helper_arg = MArgument{helper, Type::Int64};
        res.result.emplace_back(X86Subtype::vextract128, helper_vec_arg, a1,
                                MArgument(static_cast<u8>(1)));
        ASSERT(a1.isReg());
        res.result.emplace_back(
            GVecSubtype::vadd, helper_vec_arg, helper_vec_arg,
            MArgument{a1.reg.retype(Type::Int64x2), Type::Int64x2});

        res.result.emplace_back(GBaseSubtype::mov, helper_arg, helper_vec_arg);
        res.result.emplace_back(X86Subtype::vpextr, res_reg, helper_vec_arg,
                                MArgument(static_cast<u8>(1)));
        res.result.emplace_back(GArithSubtype::add2, res_reg, helper_arg);
        break;
      }
      case Type::Int32x8:
      case Type::Int32x4: {
        // fold the upper half onto the lower one, then add the 4 lanes in
        // a gpr
        auto vec128 = a1;
        if (a1.ty == Type::Int32x8) {
          auto helper_vec = data.alloc.get_new_register(Type::Int32x4);
          vec128 = MArgument{helper_vec, Type::Int32x4};
          res.result.emplace_back(X86Subtype::vextract128, vec128, a1,
                                  MArgument(static_cast<u8>(1)));
          ASSERT(a1.isReg());
          res.result.emplace_back(
              GVecSubtype::vadd, vec128, vec128,
              MArgument{a1.reg.retype(Type::Int32x4), Type::Int32x4});
        }
        res.result.emplace_back(X86Subtype::vpextr, res_reg, vec128,
                                MArgument(static_cast<u8>(0)));
        for (u8 lane = 1; lane < 4; lane++) {
          auto lane_reg = data.alloc.get_new_register(Type::Int32);
          auto lane_arg = MArgument{lane_reg, Type::Int32};
          res.result.emplace_back(X86Subtype::vpextr, lane_arg, vec128,
                                  MArgument(lane));
          res.result.emplace_back(GArithSubtype::add2, res_reg, lane_arg);
        }
        break;
      }
      case Type::Int8x16:
      case Type::Int8x32:
      case Type::Int16x8:
      case Type::Int16x16: {
        // fold the halves onto each other with shuffles (raw bits, so the
        // 128 bit value is treated as 4 dwords) until lane 0 holds the sum
        const bool bytes = a1.ty == Type::Int8x16 || a1.ty == Type::Int8x32;
        const Type v128 = bytes ? Type::Int8x16 : Type::Int16x8;
        auto vec128 = a1;
        if (a1.ty != v128) {
          auto helper_vec = data.alloc.get_new_register(v128);
          vec128 = MArgument{helper_vec, v128};
          res.result.emplace_back(X86Subtype::vextract128, vec128, a1,
                                  MArgument(static_cast<u8>(1)));
          ASSERT(a1.isReg());
          res.result.emplace_back(GVecSubtype::vadd, vec128, vec128,
                                  MArgument{a1.reg.retype(v128), v128});
        }
        auto as_dwords = [](MArgument v) {
          return MArgument{v.reg.retype(Type::Int32x4), Type::Int32x4};
        };
        auto fold = [&](bool shift, u8 imm) {
          auto moved_reg = data.alloc.get_new_register(Type::Int32x4);
          auto moved = MArgument{moved_reg, Type::Int32x4};
          if (shift) {
            res.result.emplace_back(X86Subtype::psrl, moved, as_dwords(vec128),
                                    MArgument::Int(imm, Type::Int8));
          } else {
            res.result.emplace_back(X86Subtype::vpshuf, moved,
                                    as_dwords(vec128), MArgument(imm));
          }
          auto next = MArgument{data.alloc.get_new_register(v128), v128};
          res.result.emplace_back(GVecSubtype::vadd, next, vec128,
                                  MArgument{moved_reg.retype(v128), v128});
          vec128 = next;
        };
        fold(false, 0xEE);
        fold(false, 0x55);
        fold(true, 16);
        if (bytes) {
          fold(true, 8);
        }
        auto lane_reg = data.alloc.get_new_register(Type::Int32);
        res.result.emplace_back(
            X86Subtype::vpextr, MArgument{lane_reg, Type::Int32},
            as_dwords(vec128), MArgument(static_cast<u8>(0)));
        const Type scalar = bytes ? Type::Int8 : Type::Int16;
        res.result.emplace_back(GBaseSubtype::mov, res_reg,
                                MArgument{lane_reg.retype(scalar), scalar});
        break;
      }
    }
    return true;
  };
  // halves the vector until lane 0 holds the result
  static constexpr auto lower_halving = [](fir::Instr hred_instr,
                                           MatchResult &res,
                                           ExtraMatchData &data) {
    const auto op = static_cast<fir::ReduceInstrSubType>(hred_instr->subtype);
    auto res_reg = valueToArg(hred_instr.result(), res.result, data.alloc);
    auto a1 = valueToArg(hred_instr->args[0], res.result, data.alloc);
    ASSERT(a1.isReg());

    Type v128 = Type::INVALID;
    bool wide = false;
    switch (a1.ty) {
      default:
        TODO("horizontal reduce of this vector type");
      case Type::Int32x4:
        v128 = Type::Int32x4;
        break;
      case Type::Int32x8:
        v128 = Type::Int32x4;
        wide = true;
        break;
      case Type::Int16x8:
        v128 = Type::Int16x8;
        break;
      case Type::Int16x16:
        v128 = Type::Int16x8;
        wide = true;
        break;
      case Type::Int8x16:
        v128 = Type::Int8x16;
        break;
      case Type::Int8x32:
        v128 = Type::Int8x16;
        wide = true;
        break;
      case Type::Int64x2:
        v128 = Type::Int64x2;
        break;
      case Type::Int64x4:
        v128 = Type::Int64x2;
        wide = true;
        break;
      case Type::Float32x4:
        v128 = Type::Float32x4;
        break;
      case Type::Float32x8:
        v128 = Type::Float32x4;
        wide = true;
        break;
      case Type::Float64x2:
        v128 = Type::Float64x2;
        break;
      case Type::Float64x4:
        v128 = Type::Float64x2;
        wide = true;
        break;
    }
    auto combine = [&](MArgument dst, MArgument a, MArgument b) {
      switch (op) {
        case fir::ReduceInstrSubType::Add:
          res.result.emplace_back(GVecSubtype::vadd, dst, a, b);
          break;
        case fir::ReduceInstrSubType::INVALID:
        case fir::ReduceInstrSubType::Mul:
          UNREACH();
        case fir::ReduceInstrSubType::And:
          res.result.emplace_back(GVecSubtype::vAnd, dst, a, b);
          break;
        case fir::ReduceInstrSubType::Or:
          res.result.emplace_back(GVecSubtype::vOr, dst, a, b);
          break;
        case fir::ReduceInstrSubType::Xor:
          res.result.emplace_back(GVecSubtype::vXor, dst, a, b);
          break;
        case fir::ReduceInstrSubType::SMin:
          res.result.emplace_back(GVecSubtype::vSMin, dst, a, b);
          break;
        case fir::ReduceInstrSubType::SMax:
          res.result.emplace_back(GVecSubtype::vSMax, dst, a, b);
          break;
        case fir::ReduceInstrSubType::UMin:
          res.result.emplace_back(GVecSubtype::vUMin, dst, a, b);
          break;
        case fir::ReduceInstrSubType::UMax:
          res.result.emplace_back(GVecSubtype::vUMax, dst, a, b);
          break;
        case fir::ReduceInstrSubType::FMin:
          res.result.emplace_back(GVecSubtype::fMin, dst, a, b);
          break;
        case fir::ReduceInstrSubType::FMax:
          res.result.emplace_back(GVecSubtype::fMax, dst, a, b);
          break;
      }
    };
    auto fresh = [&] {
      return MArgument{data.alloc.get_new_register(v128), v128};
    };

    auto cur = a1;
    if (wide) {
      auto hi = fresh();
      res.result.emplace_back(X86Subtype::vextract128, hi, a1,
                              MArgument(static_cast<u8>(1)));
      auto next = fresh();
      combine(next, hi, MArgument{a1.reg.retype(v128), v128});
      cur = next;
    } else {
      cur = MArgument{a1.reg.retype(v128), v128};
    }
    // vpshufd works on the raw bits, so the lanes are seen as dwords
    auto as_dwords = [](MArgument v) {
      return MArgument{v.reg.retype(Type::Int32x4), Type::Int32x4};
    };
    auto fold = [&](u8 imm) {
      auto moved_reg = data.alloc.get_new_register(Type::Int32x4);
      auto moved = MArgument{moved_reg, Type::Int32x4};
      res.result.emplace_back(X86Subtype::vpshuf, moved, as_dwords(cur),
                              MArgument(imm));
      auto next = fresh();
      combine(next, cur, MArgument{moved_reg.retype(v128), v128});
      cur = next;
    };
    // lanes shifted out are zero, only lane 0 of the result is read
    auto fold_shift = [&](u8 bits) {
      auto moved_reg = data.alloc.get_new_register(Type::Int32x4);
      auto moved = MArgument{moved_reg, Type::Int32x4};
      res.result.emplace_back(X86Subtype::psrl, moved, as_dwords(cur),
                              MArgument::Int(bits, Type::Int8));
      auto next = fresh();
      combine(next, cur, MArgument{moved_reg.retype(v128), v128});
      cur = next;
    };
    fold(0xEE);
    if (v128 != Type::Int64x2 && v128 != Type::Float64x2) {
      fold(0x55);
    }
    const bool words = v128 == Type::Int16x8 || v128 == Type::Int8x16;
    if (words) {
      fold_shift(16);
    }
    if (v128 == Type::Int8x16) {
      fold_shift(8);
    }
    if (words) {
      auto lane_reg = data.alloc.get_new_register(Type::Int32);
      res.result.emplace_back(X86Subtype::vpextr,
                              MArgument{lane_reg, Type::Int32}, as_dwords(cur),
                              MArgument(static_cast<u8>(0)));
      const Type scalar = v128 == Type::Int8x16 ? Type::Int8 : Type::Int16;
      res.result.emplace_back(GBaseSubtype::mov, res_reg,
                              MArgument{lane_reg.retype(scalar), scalar});
      return true;
    }
    if (res_reg.is_fp()) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg, cur);
    } else {
      res.result.emplace_back(X86Subtype::vpextr, res_reg, cur,
                              MArgument(static_cast<u8>(0)));
    }
    return true;
  };
  rules.add(pm::ReduceAdd, [](fir::Instr instr, MatchResult &res,
                              ExtraMatchData &data) {
    switch (instr->args[0].get_type()->as_vec().type) {
      case fir::VectorType::SubType::Integer: {
        const auto bits = instr->args[0].get_type()->get_bitwidth();
        const auto elem_bits = instr->args[0].get_type()->as_vec().bitwidth;
        if ((elem_bits == 32 || elem_bits == 64) &&
            (bits == 128 || bits == 256)) {
          return lower_halving(instr, res, data);
        }
        break;
      }
      case fir::VectorType::SubType::Floating:
        break;
    }
    return lower_hadd(instr, res, data);
  });
  rules.add(pm::ReduceMul, lower_hmul);
  rules.add(pm::ReduceAnd, lower_halving);
  rules.add(pm::ReduceOr, lower_halving);
  rules.add(pm::ReduceXor, lower_halving);
  rules.add(pm::ReduceSMin, lower_halving);
  rules.add(pm::ReduceSMax, lower_halving);
  rules.add(pm::ReduceUMin, lower_halving);
  rules.add(pm::ReduceUMax, lower_halving);
  rules.add(pm::ReduceFMin, lower_halving);
  rules.add(pm::ReduceFMax, lower_halving);
  rules.add(pm::Concat, [](fir::Instr concat_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(concat_instr.result(), res.result, data.alloc);
    auto a1 = valueToArg(concat_instr->args[0], res.result, data.alloc);
    auto a2 = valueToArg(concat_instr->args[1], res.result, data.alloc);
    ASSERT(a1.ty == a2.ty);
    switch (a1.ty) {
      default:
        TODO("invalid?");
      case Type::Int8x4: {
        // the low dwords of both end up next to each other
        ASSERT(a1.isReg() && a2.isReg());
        auto dwords = [](MArgument v) {
          return MArgument{v.reg.retype(Type::Int32x4), Type::Int32x4};
        };
        res.result.emplace_back(X86Subtype::punpckl, dwords(res_reg),
                                dwords(a1), dwords(a2));
        break;
      }
      case Type::Float32x2:
      case Type::Int8x8:
      case Type::Int16x4:
      case Type::Int32x2:
        res.result.emplace_back(GBaseSubtype::mov, res_reg, a1);
        res.result.emplace_back(X86Subtype::movlhps, res_reg, res_reg, a2);
        break;
      case Type::Int64x2:
      case Type::Float64x2:
      case Type::Int32x4:
      case Type::Float32x4:
      case Type::Int16x8:
      case Type::Int8x16:
        ASSERT(a1.isReg());
        res.result.emplace_back(
            X86Subtype::vinsert128, res_reg,
            MArgument(a1.reg.retype(res_reg.ty), res_reg.ty), a2,
            MArgument(static_cast<u8>(1)));
        break;
      case Type::Float32x8:
      case Type::Int32x8:
      case Type::Float64x4:
      case Type::Int64x4:
        fmt::println("{:cd}", *concat_instr->get_parent()->get_parent().func);
        ASSERT_M(data.config.target.features.avx512f,
                 "Need avx512 support for this");
        fmt::println("{:cd}", concat_instr->args[0].get_type());
        fmt::println("{:cd}", concat_instr);
        TODO("impl");
        break;
    }
    return true;
  });
  rules.add(pm::ExtractHigh, [](fir::Instr extract_instr, MatchResult &res,
                                ExtraMatchData &data) {
    auto res_reg = valueToArg(extract_instr.result(), res.result, data.alloc);
    auto a1 = valueToArg(extract_instr->args[0], res.result, data.alloc);

    switch (a1.ty) {
      default:
        TODO("invalid?");
      case Type::Float32x4:
        res.result.emplace_back(GVecSubtype::vXor, res_reg, res_reg, res_reg);
        res.result.emplace_back(X86Subtype::movhlps, res_reg, res_reg, a1);
        break;
      case Type::Int64x2:
        if (res_reg.is_vec_reg()) {
          res.result.emplace_back(GVecSubtype::vXor, res_reg, res_reg, res_reg);
          res.result.emplace_back(X86Subtype::movhlps, res_reg, res_reg, a1);
          break;
        }
        res.result.emplace_back(X86Subtype::vpextr, res_reg, a1,
                                MArgument(static_cast<u8>(1)));
        break;
      case Type::Int64x4:
      case Type::Int32x8:
      case Type::Float32x8:
      case Type::Float64x4:
      case Type::Int8x32:
      case Type::Int16x16:
        res.result.emplace_back(X86Subtype::vextract128, res_reg, a1,
                                MArgument(static_cast<u8>(1)));
        break;
      case Type::Int8x16:
      case Type::Int16x8:
      case Type::Int8x8:
      case Type::Int16x4:
      case Type::Int32x2: {
        // the upper half moves down, vpshufd works on dwords
        const bool xmm = get_size(a1.ty) == 16;
        auto dwords = [](MArgument v) {
          return MArgument{v.reg.retype(Type::Int32x4), Type::Int32x4};
        };
        res.result.emplace_back(X86Subtype::vpshuf, dwords(res_reg), dwords(a1),
                                MArgument(static_cast<u8>(xmm ? 0xEE : 0x55)));
        break;
      }
      case Type::Float32x2:
      case Type::Float64x2:
      case Type::Int32x4:
        fmt::println("{:cd}", extract_instr->args[0].get_type());
        fmt::println("{:cd}", extract_instr);
        TODO("impl");
        break;
    }
    return true;
  });
  rules.add(pm::ExtractLow, [](fir::Instr extract_instr, MatchResult &res,
                               ExtraMatchData &data) {
    auto res_reg = valueToArg(extract_instr.result(), res.result, data.alloc);
    auto a1 = valueToArg(extract_instr->args[0], res.result, data.alloc);

    switch (a1.ty) {
      default:
        TODO("invalid?");
      case Type::Float32x8:
      case Type::Float32x4:
      case Type::Int64x4:
      case Type::Float64x4:
      case Type::Int32x8:
      case Type::Float64x2:
      case Type::Int64x2:
      case Type::Int32x4:
      case Type::Int8x32:
      case Type::Int16x16:
      case Type::Int8x16:
      case Type::Int16x8:
      case Type::Int8x8:
      case Type::Int16x4:
      case Type::Int32x2:
        res.result.emplace_back(GBaseSubtype::mov, res_reg, a1);
        break;
      case Type::Float32x2:
        fmt::println("{:cd}", extract_instr->args[0].get_type());
        fmt::println("{:cd}", extract_instr);
        TODO("impl");
        break;
    }
    return true;
  });
  rules.add(pm::IntNeg, [](fir::Instr negate_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(negate_instr.result(), res.result, data.alloc);

    res.result.emplace_back(
        GBaseSubtype::mov, res_reg,
        valueToArg(negate_instr->args[0], res.result, data.alloc));
    res.result.emplace_back(GArithSubtype::neg1, res_reg);
    return true;
  });
  rules.add(pm::Shuffle, [](fir::Instr shuf_instr, MatchResult &res,
                            ExtraMatchData &data) {
    ASSERT(shuf_instr->args[2].is_constant());
    const auto &members = shuf_instr->args[2].as_constant()->as_vec().members;
    const u32 n = members.size();
    TVec<u32> mask;
    for (const auto &m : members) {
      // negative lanes are undef
      const i128 v = m->as_int();
      mask.push_back(v < 0 ? 0 : static_cast<u32>(v));
    }
    auto res_reg = valueToArg(shuf_instr.result(), res.result, data.alloc);
    auto a1 = valueToArg(shuf_instr->args[0], res.result, data.alloc);
    auto a2 = valueToArg(shuf_instr->args[1], res.result, data.alloc);
    const u32 bytes = get_size(res_reg.ty);
    const u32 elem_bytes = bytes / n;
    ASSERT(elem_bytes == 4 || elem_bytes == 8);
    ASSERT(bytes == 16 || bytes == 32);
    const Type dwords = bytes == 16 ? Type::Int32x4 : Type::Int32x8;
    const Type floats = bytes == 16 ? Type::Float32x4 : Type::Float32x8;
    auto as = [](MArgument v, Type t) { return MArgument{v.reg.retype(t), t}; };
    TVec<u32> dm;
    for (u32 m : mask) {
      for (u32 k = 0; k < elem_bytes / 4; k++) {
        dm.push_back(m * (elem_bytes / 4) + k);
      }
    }
    const u32 nd = dm.size();
    const bool from_second = dm[0] >= nd;
    const bool single = std::ranges::all_of(
        dm, [&](u32 d) { return (d >= nd) == from_second; });
    if (single) {
      // vpshufd moves dwords inside each 128 bit lane, the same in all lanes
      TVec<u32> in_lane;
      for (u32 k = 0; k < nd; k++) {
        const u32 d = dm[k] % nd;
        ASSERT_M(d / 4 == k / 4, "shuffle crosses 128 bit lanes");
        in_lane.push_back(d % 4);
        ASSERT_M(in_lane[k] == in_lane[k % 4], "shuffle differs per lane");
      }
      const u8 imm = in_lane[0] | (in_lane[1] << 2) | (in_lane[2] << 4) |
                     (in_lane[3] << 6);
      res.result.emplace_back(X86Subtype::vpshuf, as(res_reg, dwords),
                              as(from_second ? a2 : a1, dwords),
                              MArgument(imm));
      return true;
    }
    // vshufps takes the low half of the result from one source and the high
    // half from the other
    ASSERT_M(nd == 4, "two source shuffle needs 4 dwords");
    const bool swap = dm[0] >= nd;
    u8 imm = 0;
    for (u32 k = 0; k < 4; k++) {
      const bool second = dm[k] >= nd;
      ASSERT_M(second == (swap != (k >= 2)), "unsupported two source shuffle");
      imm |= static_cast<u8>((dm[k] % nd) << (2 * k));
    }
    res.result.emplace_back(X86Subtype::vshufps, as(res_reg, floats),
                            as(swap ? a2 : a1, floats),
                            as(swap ? a1 : a2, floats), MArgument(imm));
    return true;
  });
  rules.add(pm::Broadcast, [](fir::Instr broad_instr, MatchResult &res,
                              ExtraMatchData &data) {
    auto res_reg = valueToArg(broad_instr.result(), res.result, data.alloc);
    auto arg = valueToArg(broad_instr->args[0], res.result, data.alloc);
    if (arg.isImm() &&
        ((!arg.is_fp() && arg.imm == 0) ||
         (arg.is_fp() && arg.immf == 0 && !std::signbit(arg.immf)))) {
      res.result.emplace_back(GVecSubtype::vXor, res_reg, res_reg, res_reg);
      return true;
    }
    if (arg.isImm() && !arg.is_fp() && get_size(res_reg.ty) <= 32 &&
        get_size(res_reg.ty) >= 16) {
      const u32 bits = get_size(arg.ty) * 8;
      const u64 mask = bits >= 64 ? ~0ULL : ((1ULL << bits) - 1);
      if ((arg.imm & mask) == mask) {
        // all ones is a compare of a register with itself
        const Type dwords =
            get_size(res_reg.ty) == 16 ? Type::Int32x4 : Type::Int32x8;
        const auto ones = MArgument{res_reg.reg.retype(dwords), dwords};
        res.result.emplace_back(X86Subtype::vpcmpeq, ones, ones, ones);
        return true;
      }
    }
    if (res_reg.ty == Type::Int8x16 || res_reg.ty == Type::Int8x32 ||
        res_reg.ty == Type::Int16x8 || res_reg.ty == Type::Int16x16) {
      // vpbroadcastb/w read the low lane of an xmm, only the low bits of
      // the gpr matter
      auto gpr = arg;
      if (arg.isImm()) {
        gpr = MArgument{data.alloc.get_new_register(Type::Int32), Type::Int32};
        res.result.emplace_back(GBaseSubtype::mov, gpr,
                                MArgument::Int(arg.imm, Type::Int32));
      } else {
        gpr = MArgument{arg.reg.retype(Type::Int32), Type::Int32};
      }
      auto xmm =
          MArgument{data.alloc.get_new_register(Type::Float32), Type::Float32};
      res.result.emplace_back(GBaseSubtype::mov, xmm, gpr);
      res.result.emplace_back(X86Subtype::vbroadcast, res_reg, xmm);
      return true;
    }
    // TODO: better way to generate if the lowest or the upper n bits
    // set
    //  if (arg.isImm() && !arg.is_fp() && arg.imm == 1) {
    //    res.result.emplace_back(X86Subtype::vpcmpeq, res_reg, res_reg,
    //                            res_reg);
    //    res.result.emplace_back(GVecSubtype::fShr, res_reg, width-1);
    //    return true;
    //  }
    //  TODO: better to use if we know that its the n lowest bits that
    //  are set pcmpeqd       xmm0, xmm0 psrld xmm0, 32-n
    if (data.config.target.features.avx512vl) {
      switch (res_reg.ty) {
        case Type::Int32x4:
        case Type::Int32x8:
        case Type::Int64x2: {
          // broadcast from an xmm, a gpr source is not encodable
          auto helper_ty = arg.ty == Type::Int64   ? Type::Float64
                           : arg.ty == Type::Int32 ? Type::Float32
                                                   : arg.ty;
          auto helper_reg =
              MArgument(data.alloc.get_new_register(helper_ty), helper_ty);
          res.result.emplace_back(GBaseSubtype::mov, helper_reg, arg);
          res.result.emplace_back(X86Subtype::vbroadcast, res_reg, helper_reg);
          return true;
        }
        case Type::Float64x2: {
          res_reg.ty = Type::Float64x4;
          res_reg.reg.ty = Type::Float64x4;
          auto helper_reg =
              MArgument(data.alloc.get_new_register(arg.ty), arg.ty);
          res.result.emplace_back(GBaseSubtype::mov, helper_reg, arg);
          res.result.emplace_back(X86Subtype::vbroadcast, res_reg, helper_reg);
          return true;
        }
        default:
          break;
      }
    }

    auto res_reg_smoll = res_reg;
    bool can_pshuf = false;
    bool can_punpckl = false;
    bool can_vbroadcast = false;
    switch (res_reg.ty) {
      case Type::INVALID:
      case Type::Int32x4:
      case Type::Float32x4:
        can_pshuf = true;
        res_reg_smoll.ty = Type::Float32;
        res_reg_smoll.reg.ty = Type::Float32;
      case Type::Int64x2:
      case Type::Float64x2:
        can_punpckl = true;
        res_reg_smoll.ty = Type::Float64;
        res_reg_smoll.reg.ty = Type::Float64;
        break;
      case Type::Int32x8:
      case Type::Float32x8:
        // vpshufd only shuffles inside each 128 bit lane
        can_vbroadcast = true;
        res_reg_smoll.ty = Type::Float32;
        res_reg_smoll.reg.ty = Type::Float32;
        break;
      case Type::Int64x4:
      case Type::Float64x4:
      case Type::Int64x8:
      case Type::Float64x8:
        can_vbroadcast = true;
        res_reg_smoll.ty = Type::Float64;
        res_reg_smoll.reg.ty = Type::Float64;
        break;
      default:
        TODO("UNREACH");
    }
    if (can_pshuf) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg_smoll, arg);
      res.result.emplace_back(X86Subtype::vpshuf, res_reg, res_reg,
                              MArgument(static_cast<u8>(0)));
      return true;
    }
    if (can_punpckl) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg_smoll, arg);
      res.result.emplace_back(X86Subtype::punpckl, res_reg, res_reg, res_reg);
      return true;
    }
    if (can_vbroadcast) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg_smoll, arg);
      res.result.emplace_back(X86Subtype::vbroadcast, res_reg, res_reg_smoll);
      return true;
    }
    TODO("IMPL broadcast");
    return false;
  });
  rules.add(pm::FCmp, [](fir::Instr cmp_instr, MatchResult &res,
                         ExtraMatchData &data) {
    auto res_reg = data.alloc.get_register(cmp_instr.result());

    auto res_arg = MArgument(res_reg, value_type(cmp_instr.result()));

    if (is_mask_type(cmp_instr.result_type())) {
      auto a = valueToArg(cmp_instr->args[0], res.result, data.alloc);
      auto b = valueToArg(cmp_instr->args[1], res.result, data.alloc);
      res.result.emplace_back(X86Subtype::vcmp, res_arg, a, b,
                              MArgument(vcmp_predicate(cmp_instr)));
      return true;
    }
    auto arg1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
    auto arg2 =
        valueToArgPosMem(cmp_instr->args[1], res.result, data.alloc, cmp_instr);
    // auto arg2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

    GJumpSubtype op = GJumpSubtype::fcmp_oeq;

    switch (
        static_cast<fir::FCmpInstrSubType>(cmp_instr->get_instr_subtype())) {
      case fir::FCmpInstrSubType::IsNaN:
        op = GJumpSubtype::fcmp_isNaN;
        break;
      case fir::FCmpInstrSubType::OEQ:
        op = GJumpSubtype::fcmp_oeq;
        break;
      case fir::FCmpInstrSubType::OGT:
        op = GJumpSubtype::fcmp_ogt;
        break;
      case fir::FCmpInstrSubType::OGE:
        op = GJumpSubtype::fcmp_oge;
        break;
      case fir::FCmpInstrSubType::OLT:
        op = GJumpSubtype::fcmp_olt;
        break;
      case fir::FCmpInstrSubType::OLE:
        op = GJumpSubtype::fcmp_ole;
        break;
      case fir::FCmpInstrSubType::ONE:
        op = GJumpSubtype::fcmp_one;
        break;
      case fir::FCmpInstrSubType::ORD:
        op = GJumpSubtype::fcmp_ord;
        break;
      case fir::FCmpInstrSubType::UNO:
        op = GJumpSubtype::fcmp_uno;
        break;
      case fir::FCmpInstrSubType::UEQ:
        op = GJumpSubtype::fcmp_ueq;
        break;
      case fir::FCmpInstrSubType::UGT:
        op = GJumpSubtype::fcmp_ugt;
        break;
      case fir::FCmpInstrSubType::UGE:
        op = GJumpSubtype::fcmp_uge;
        break;
      case fir::FCmpInstrSubType::ULT:
        op = GJumpSubtype::fcmp_ult;
        break;
      case fir::FCmpInstrSubType::ULE:
        op = GJumpSubtype::fcmp_ule;
        break;
      case fir::FCmpInstrSubType::UNE:
        op = GJumpSubtype::fcmp_une;
        break;
      case fir::FCmpInstrSubType::INVALID:
      case fir::FCmpInstrSubType::AlwFalse:
      case fir::FCmpInstrSubType::AlwTrue:
        TODO("");
        break;
    }
    res.result.emplace_back(op, res_arg, arg1, arg2);
    return true;
  });
  rules.add(pm::ICmp, [](fir::Instr cmp_instr, MatchResult &res,
                         ExtraMatchData &data) {
    // TODO: kinda sus
    auto res_reg = data.alloc.get_register(cmp_instr.result());

    auto res_arg = MArgument(res_reg, value_type(cmp_instr.result()));

    auto arg1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
    auto arg2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

    if (is_mask_type(cmp_instr.result_type())) {
      vector_icmp(cmp_instr, res_arg, arg1, arg2, res, data);
      return true;
    }

    GJumpSubtype op = GJumpSubtype::icmp_eq;

    switch (
        static_cast<fir::ICmpInstrSubType>(cmp_instr->get_instr_subtype())) {
      case fir::ICmpInstrSubType::SLT:
        op = GJumpSubtype::icmp_slt;
        break;
      case fir::ICmpInstrSubType::ULT:
        op = GJumpSubtype::icmp_ult;
        break;
      case fir::ICmpInstrSubType::NE:
        op = GJumpSubtype::icmp_ne;
        break;
      case fir::ICmpInstrSubType::EQ:
        op = GJumpSubtype::icmp_eq;
        break;
      case fir::ICmpInstrSubType::SGT:
        op = GJumpSubtype::icmp_sgt;
        break;
      case fir::ICmpInstrSubType::UGT:
        op = GJumpSubtype::icmp_ugt;
        break;
      case fir::ICmpInstrSubType::UGE:
        op = GJumpSubtype::icmp_uge;
        break;
      case fir::ICmpInstrSubType::ULE:
        op = GJumpSubtype::icmp_ule;
        break;
      case fir::ICmpInstrSubType::SGE:
        op = GJumpSubtype::icmp_sge;
        break;
      case fir::ICmpInstrSubType::SLE:
        op = GJumpSubtype::icmp_sle;
        break;
      case fir::ICmpInstrSubType::INVALID:
        UNREACH();
    }
    res.result.emplace_back(op, res_arg, arg1, arg2);
    return true;
  });
  rules.add(pm::Branch, [](fir::Instr branch_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto bb_with_args = branch_instr->bbs[0];
    auto target_bb = branch_instr->bbs[0].bb;
    ASSERT(bb_with_args.args.size() == target_bb->args.size());
    generate_bb_args(bb_with_args, res, data);
    res.result.push_back(MInstr::jmp(data.bbs[bb_with_args.bb]));
    return true;
  });
  rules.add(pm::CondBranch, [](fir::Instr branch_instr, MatchResult &res,
                               ExtraMatchData &data) {
    auto cond = valueToArg(branch_instr->args[0], res.result, data.alloc);
    {
      auto &bb_with_args = branch_instr->bbs[0];
      auto target_bb = branch_instr->bbs[0].bb;
      ASSERT(bb_with_args.args.size() == target_bb->args.size());
      generate_bb_args(bb_with_args, res, data);
      res.result.push_back(MInstr::cJmp(cond, data.bbs[bb_with_args.bb]));
    }

    {
      auto &bb2_with_args = branch_instr->bbs[1];
      auto target_bb2 = branch_instr->bbs[1].bb;
      ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
      generate_bb_args(bb2_with_args, res, data);
      res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
    }
    return true;
  });
  rules.add(pm::Switch, [](fir::Instr branch_instr, MatchResult &res,
                           ExtraMatchData &data) {
    // fmt::println("{}", branch_instr);
    auto value = valueToArg(branch_instr->args.back(), res.result, data.alloc);
    for (size_t instr_id = 0; instr_id < branch_instr->bbs.size() - 1;
         instr_id++) {
      auto &bb_with_args = branch_instr->bbs[instr_id];
      auto target_bb = branch_instr->bbs[instr_id].bb;
      ASSERT(bb_with_args.args.size() == target_bb->args.size());
      generate_bb_args(bb_with_args, res, data);
      auto cond_val =
          valueToArg(branch_instr->args[instr_id], res.result, data.alloc);
      res.result.push_back(
          MInstr::cJmp_eq(value, cond_val, data.bbs[bb_with_args.bb]));
    }
    {
      size_t instr_id = branch_instr->bbs.size() - 1;
      auto &bb_with_args = branch_instr->bbs[instr_id];
      auto target_bb = branch_instr->bbs[instr_id].bb;
      ASSERT(bb_with_args.args.size() == target_bb->args.size());
      generate_bb_args(bb_with_args, res, data);
      // auto cond_val =
      //     valueToArg(branch_instr->args[instr_id], res.result,
      //     data.alloc);
      // res.result.push_back(
      //     MInstr::cJmp_eq(value, cond_val,
      //     data.bbs[bb_with_args.bb]));
      res.result.push_back(MInstr::jmp(data.bbs[bb_with_args.bb]));
    }
    return true;
  });
  rules.add(pm::Return, [](fir::Instr ret_instr, MatchResult &res,
                           ExtraMatchData &data) {
    if (data.func.has_dynamic_alloca) {
      res.result.emplace_back(GBaseSubtype::mov,
                              MArgument(VReg::RSP(), Type::Int64),
                              MArgument(data.saved_sp, Type::Int64));
    } else if (data.static_alloca_size > 0) {
      if (data.static_alloca_size < 255) {
        res.result.emplace_back(
            GArithSubtype::add2, MArgument(VReg::RSP(), Type::Int64),
            MArgument(static_cast<u8>(data.static_alloca_size)));
      } else {
        res.result.emplace_back(GArithSubtype::add2,
                                MArgument(VReg::RSP(), Type::Int64),
                                MArgument(data.static_alloca_size));
      }
    }
    if (!ret_instr->has_args()) {
      res.result.emplace_back(GBaseSubtype::ret);
      return true;
    }
    if (ret_instr->args.size() > 1) {
      // every value takes the next free return register of its own class
      TVec<MArgument> vals;
      for (const auto &arg : ret_instr->args) {
        vals.push_back(valueToArg(arg, res.result, data.alloc));
      }
      MInstr ret{GBaseSubtype::ret};
      ASSERT_M(vals.size() <= 4, "more return values than the 2 + 2 registers");
      u32 n_gpr = 0;
      u32 n_sse = 0;
      for (size_t i = 0; i < vals.size(); i++) {
        const auto fir_ty = ret_instr->args[i].get_type();
        const bool sse = fir_ty->is_float() || fir_ty->is_vec();
        const auto ty = vals[i].ty;
        const auto reg = sse ? (n_sse++ == 0 ? CReg::mm0 : CReg::mm1)
                             : (n_gpr++ == 0 ? CReg::A : CReg::D);
        ASSERT_M(n_gpr <= 2 && n_sse <= 2,
                 "more return values than the 2 + 2 registers");
        const auto ret_arg = MArgument(VReg{reg, ty}, ty);
        res.result.emplace_back(GBaseSubtype::mov, ret_arg, vals[i]);
        ret.args[ret.n_args++] = ret_arg;
      }
      res.result.push_back(ret);
      return true;
    }

    ASSERT(ret_instr->args.size() == 1);
    auto ret_val = valueToArg(ret_instr->args[0], res.result, data.alloc);
    auto is_float_val = ret_val.is_vec_reg();

    if (!is_float_val && (!ret_val.isReg() || !ret_val.reg.is_concrete() ||
                          ret_val.reg.c_reg() != CReg::A)) {
      auto converted_type = convert_type(ret_instr.result_type());
      auto res_reg = VReg{CReg::A, converted_type};
      auto res_arg = MArgument(res_reg, converted_type);
      res.result.emplace_back(GBaseSubtype::mov, res_arg, ret_val);
      res.result.emplace_back(GBaseSubtype::ret, res_arg);
    } else if (is_float_val &&
               (!ret_val.isReg() || !ret_val.reg.is_concrete() ||
                ret_val.reg.c_reg() != CReg::mm0)) {
      auto converted_type = convert_type(ret_instr.result_type());
      auto res_reg = VReg{CReg::mm0, converted_type};
      auto res_arg = MArgument(res_reg, converted_type);
      res.result.emplace_back(GBaseSubtype::mov, res_arg, ret_val);
      res.result.emplace_back(GBaseSubtype::ret, res_arg);
    } else {
      res.result.emplace_back(GBaseSubtype::ret, ret_val);
    }
    return true;
  });
  rules.add(pm::SExt, [](fir::Instr sext_instr, MatchResult &res,
                         ExtraMatchData &data) {
    auto val = valueToArg(sext_instr->args[0], res.result, data.alloc);
    auto res_reg = valueToArg(sext_instr.result(), res.result, data.alloc);

    if (is_mask_type(sext_instr->args[0].get_type())) {
      // a mask lane is already all ones or all zeros
      if (get_size(val.ty) != get_size(res_reg.ty)) {
        fmt::println("{:cd}", sext_instr);
        TODO("sext of a mask to another lane width");
      }
      res.result.emplace_back(GBaseSubtype::mov, res_reg, val);
      return true;
    }
    res.result.emplace_back(GConvSubtype::mov_sx, res_reg, val);
    return true;
  });
  rules.add(pm::ZExt, [](fir::Instr zext_instr, MatchResult &res,
                         ExtraMatchData &data) {
    auto val = valueToArg(zext_instr->args[0], res.result, data.alloc);
    auto res_reg = valueToArg(zext_instr.result(), res.result, data.alloc);

    if (is_mask_type(zext_instr->args[0].get_type())) {
      // all ones lanes shifted down to one
      const bool wide =
          res_reg.ty == Type::Int64x2 || res_reg.ty == Type::Int64x4;
      const bool dword =
          res_reg.ty == Type::Int32x4 || res_reg.ty == Type::Int32x8;
      if (get_size(val.ty) != get_size(res_reg.ty) || !(wide || dword)) {
        fmt::println("{:cd}", zext_instr);
        TODO("zext of a mask to this vector type");
      }
      res.result.emplace_back(X86Subtype::psrl, res_reg, val,
                              MArgument(static_cast<u8>(wide ? 63 : 31)));
      return true;
    }
    res.result.emplace_back(GConvSubtype::mov_zx, res_reg, val);
    return true;
  });
  rules.add(pm::Conversion, [](fir::Instr conversion_instr, MatchResult &res,
                               ExtraMatchData &data) {
    auto val = valueToArg(conversion_instr->args[0], res.result, data.alloc);
    auto res_reg =
        valueToArg(conversion_instr.result(), res.result, data.alloc);
    auto res_opcode = GConvSubtype::FL2SI;
    switch (static_cast<fir::ConversionSubType>(conversion_instr->subtype)) {
      case fir::ConversionSubType::INVALID:
        UNREACH();
      case fir::ConversionSubType::BitCast:
        res.result.emplace_back(GBaseSubtype::mov, res_reg, val);
        return true;
      case fir::ConversionSubType::FPTOUI:
        if (get_size(res_reg.ty) == 8) {
          // _Z4testv:
          //         vmovsd/vmovss     hF64, Input
          //         cvttsd2si/cttss2s1 h64, hF64
          //         mov       g64, h64
          //         subsd     hF64, hF64, 0x43e0000000000000
          //         cvttsd2si res, hF64
          //         sar       g64, 63
          //         and       res, g64
          //         or        res, h64
          auto doub = get_size(val.ty) == 64;
          auto reg_ty = val.ty;
          auto hf = MArgument(data.alloc.get_new_register(reg_ty), reg_ty);
          auto h64 =
              MArgument(data.alloc.get_new_register(Type::Int64), Type::Int64);
          auto g64 =
              MArgument(data.alloc.get_new_register(Type::Int64), Type::Int64);
          res.result.emplace_back(GBaseSubtype::mov, hf, val);
          res.result.emplace_back(GConvSubtype::FL2SI, h64, hf);
          res.result.emplace_back(GBaseSubtype::mov, g64, h64);
          res.result.emplace_back(
              GVecSubtype::vsub, hf, hf,
              MArgument(doub ? static_cast<f64>(0x43e0000000000000)
                             : static_cast<f64>(0x5f000000)));
          res.result.emplace_back(GConvSubtype::FL2SI, res_reg, hf);
          res.result.emplace_back(GArithSubtype::sar2, g64,
                                  MArgument(static_cast<u8>(63)));
          res.result.emplace_back(GArithSubtype::land2, res_reg, g64);
          res.result.emplace_back(GArithSubtype::lor2, res_reg, h64);
          return true;
        }
        res_opcode = GConvSubtype::FL2UI;
        break;
      case fir::ConversionSubType::FPTOSI:
        res_opcode = GConvSubtype::FL2SI;
        break;
      case fir::ConversionSubType::UITOFP:
        res_opcode = GConvSubtype::UI2FL;
        break;
      case fir::ConversionSubType::SITOFP:
        res_opcode = GConvSubtype::SI2FL;
        break;
      case fir::ConversionSubType::FPEXT:
        res_opcode = GConvSubtype::F64_ext;
        break;
      case fir::ConversionSubType::FPTRUNC:
        res_opcode = GConvSubtype::F32_trunc;
        break;
      case fir::ConversionSubType::PtrToInt:
      case fir::ConversionSubType::IntToPtr:
        res.result.emplace_back(GBaseSubtype::mov, res_reg, val);
        return true;
    }

    res.result.emplace_back(res_opcode, res_reg, val);
    return true;
  });
  rules.add(pm::ITrunc, [](fir::Instr itrunc_instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto val = valueToArg(itrunc_instr->args[0], res.result, data.alloc);
    auto res_reg = valueToArg(itrunc_instr.result(), res.result, data.alloc);
    if (itrunc_instr.result_type()->is_vec()) {
      lower_vector_itrunc(res, data, res_reg, val);
      return true;
    }
    res.result.emplace_back(GConvSubtype::itrunc, res_reg, val);
    // i1 lives in a byte that consumers (cjmp, zext) assume to be 0/1
    if (itrunc_instr.result_type()->is_int() &&
        itrunc_instr.result_type()->as_int() == 1) {
      res.result.emplace_back(GArithSubtype::land2, res_reg,
                              MArgument::Int(1, res_reg.ty));
    }
    return true;
  });
  rules.add(pm::Call, [](fir::Instr call_instr, MatchResult &res,
                         ExtraMatchData &data) {
    setup_callargs(call_instr, res, data);

    MArgument calee;
    bool is_var_arg = false;
    if (call_instr->args[0].is_constant()) {
      calee = valueToArgPtr(call_instr->args[0], Type::Int64, res.result,
                            data.alloc);
      const auto callee_const = call_instr->args[0].as_constant();
      is_var_arg = callee_const->is_func() &&
                   callee_const->as_func()->attribs.variadic;
    } else {
      calee = valueToArg(call_instr->args[0], res.result, data.alloc);
    }

    res.result.emplace_back(GBaseSubtype::invoke, calee);
    res.result.back().is_var_arg_call = is_var_arg;

    if (call_instr.is_multi_result()) {
      // result i is the next return register of its class, an unused result
      // still gets its ret_setup to keep the positions
      for (u8 i = 0; i < call_instr.n_results(); i++) {
        auto res_reg = valueToArg(call_instr.result(i), res.result, data.alloc);
        res.result.emplace_back(GBaseSubtype::ret_setup, res_reg);
      }
      return true;
    }

    auto res_type = call_instr.result_type();
    if (res_type->is_void()) {
      return true;
    }
    if (res_type->is_int() || res_type->is_ptr() || res_type->is_float() ||
        res_type->is_vec()) {
      auto res_reg = valueToArg(call_instr.result(), res.result, data.alloc);
      res.result.emplace_back(GBaseSubtype::ret_setup, res_reg);
      return true;
    }
    TODO("impl ret value");
  });
  rules.add(pm::FloatNeg, [](fir::Instr f_neg_instr, MatchResult &res,
                             ExtraMatchData &data) {
    auto a1 = valueToArg(f_neg_instr->args[0], res.result, data.alloc);
    auto res_reg = valueToArg(f_neg_instr.result(), res.result, data.alloc);

    const bool is_f64 =
        res_reg.ty == Type::Float64 || res_reg.ty == Type::Float64x2 ||
        res_reg.ty == Type::Float64x4 || res_reg.ty == Type::Float64x8;
    MArgument a2;
    if (is_f64) {
      u64 val = 0x8000000000000000UL;
      f64 v;
      std::memcpy(&v, &val, sizeof(f64));
      a2 = MArgument{v};
    } else {
      u32 val = 0x80000000ULL;
      f32 v;
      std::memcpy(&v, &val, sizeof(f32));
      a2 = MArgument{v};
    }
    if (res_reg.ty != Type::Float32 && res_reg.ty != Type::Float64) {
      // splat the scalar sign mask over all lanes
      auto scalar_ty = is_f64 ? Type::Float64 : Type::Float32;
      auto scalar =
          MArgument(data.alloc.get_new_register(scalar_ty), scalar_ty);
      res.result.emplace_back(GBaseSubtype::mov, scalar, a2);
      auto mask =
          MArgument(data.alloc.get_new_register(res_reg.ty), res_reg.ty);
      res.result.emplace_back(X86Subtype::vbroadcast, mask, scalar);
      a2 = mask;
    }
    res.result.emplace_back(GVecSubtype::vXor, res_reg, a1, a2);
    return true;
  });
  rules.add(pm::Unreachable,
            [](fir::Instr, MatchResult &res, ExtraMatchData &data) {
              (void)data;
              (void)res;
              // TODO: prob nicer way to handle this
              res.result.emplace_back(GBaseSubtype::unreach);
              return true;
            });
  rules.add(pm::InsertElement, [](fir::Instr insert_instr, MatchResult &res,
                                  ExtraMatchData &data) {
    ASSERT(insert_instr->args[2].is_constant() &&
           insert_instr->args[2].as_constant()->is_int());
    auto indx = insert_instr->args[2].as_constant()->as_int();

    if (insert_instr->args[0].get_type()->is_vec()) {
      auto input_vec =
          valueToArg(insert_instr->args[0], res.result, data.alloc);
      auto input = valueToArg(insert_instr->args[1], res.result, data.alloc);
      if (!input.isReg()) {
        // constant lane value: materialize it first
        auto const_reg = data.alloc.get_new_register(input.ty);
        res.result.emplace_back(GBaseSubtype::mov,
                                MArgument(const_reg, input.ty), input);
        input = MArgument(const_reg, input.ty);
      }
      auto target = valueToArg(insert_instr.result(), res.result, data.alloc);
      auto vec_ty = insert_instr->args[0].get_type()->as_vec();
      (void)vec_ty;
      // lane i of the FIR vector lives in lane i of the register
      auto rev_indx = indx;
      auto get_xmm_version_ty = [](Type t) {
        if (t == Type::Float32) {
          return Type::Float32x4;
        }
        if (t == Type::Int32) {
          return Type::Int32x4;
        }
        if (t == Type::Float64) {
          return Type::Float64x2;
        }
        if (t == Type::Int64) {
          return Type::Int64x2;
        }
        fmt::println("unreach? {}", t);
        TODO("UNREACH");
      };
      if (input.ty == Type::Int8 || input.ty == Type::Int16 ||
          input.ty == Type::Int32 ||
          (input.ty == Type::Int64 && target.ty != Type::Int64x4) ||
          (input.ty == Type::Float32 &&
           (rev_indx >= 4 || get_size(target.ty) == 32))) {
        insert_lane_generic(insert_instr, res, data, target, input_vec, input,
                            static_cast<u32>(rev_indx));
        return true;
      }
      auto helper_ty = get_xmm_version_ty(input.ty);
      if ((target.ty == Type::Int64x4 || target.ty == Type::Float64x4) &&
          (input.ty == Type::Int64 || input.ty == Type::Float64)) {
        // patch the lane in its 128 bit half and put the half back
        auto half = static_cast<u8>(rev_indx >> 1);
        auto half_reg = data.alloc.get_new_register(helper_ty);
        auto half_arg = MArgument(half_reg, helper_ty);
        res.result.emplace_back(GBaseSubtype::mov, target, input_vec);
        res.result.emplace_back(X86Subtype::vextract128, half_arg, target,
                                MArgument(half));
        // vpinsr takes the integer lane from a general purpose register
        auto lane_src = input.ty == Type::Int64
                            ? input
                            : MArgument(input.reg.retype(helper_ty), helper_ty);
        res.result.emplace_back(X86Subtype::vpinsr, half_arg, half_arg,
                                lane_src,
                                MArgument(static_cast<u8>(rev_indx & 1)));
        res.result.emplace_back(X86Subtype::vinsert128, target, target,
                                half_arg, MArgument(half));
        return true;
      }
      if (rev_indx == 0) {
        ASSERT(input.isReg())
        res.result.emplace_back(GBaseSubtype::mov, target, input_vec);
        if (input.ty == Type::Float64 || input.ty == Type::Int64) {
          // vinsertps only moves 32 bits, a 64 bit lane 0 is a vmovsd
          res.result.emplace_back(
              X86Subtype::vpinsr, target, target,
              MArgument(input.reg.retype(helper_ty), helper_ty),
              MArgument(static_cast<u8>(0)));
        } else {
          res.result.emplace_back(
              X86Subtype::vinsertps, target, target,
              MArgument(input.reg.retype(helper_ty), helper_ty),
              MArgument(static_cast<u8>(
                  0b00'00'0000)));  // src lane0, dst lane0, zmask=0000
        }
      } else {
        ASSERT(input.isReg())
        // Start by copying the untouched vector into place, then patch the
        // single lane we're inserting.
        res.result.emplace_back(GBaseSubtype::mov, target, input_vec);

        if (input.ty == Type::Float64 || input.ty == Type::Int64) {
          if (rev_indx == 1) {
            // second-to-last lane lives in the low 128 bits -> vpinsr in
            // place
            res.result.emplace_back(
                X86Subtype::vpinsr, target, target,
                MArgument(input.reg.retype(helper_ty), helper_ty),
                MArgument(static_cast<u8>(1)));
          } else if (rev_indx == 2) {
            // first lane of the high 128 bits: build the high half in a
            // scratch xmm and vinsert128 it back in.
            auto helper_reg = data.alloc.get_new_register(helper_ty);
            auto high_half = MArgument(helper_reg, helper_ty);
            res.result.emplace_back(
                GBaseSubtype::mov, high_half,
                MArgument(input.reg.retype(helper_ty), helper_ty));
            res.result.emplace_back(X86Subtype::vinsert128, target, target,
                                    high_half, MArgument(static_cast<u8>(1)));
          } else if (rev_indx == 3) {
            // second lane of the high 128 bits: pull the high half out,
            // patch its top lane, and reinsert it.
            auto helper_reg = data.alloc.get_new_register(helper_ty);
            auto high_half = MArgument(helper_reg, helper_ty);
            res.result.emplace_back(X86Subtype::vextract128, high_half, target,
                                    MArgument(static_cast<u8>(1)));
            res.result.emplace_back(
                X86Subtype::vpinsr, high_half, high_half,
                MArgument(input.reg.retype(helper_ty), helper_ty),
                MArgument(static_cast<u8>(1)));
            res.result.emplace_back(X86Subtype::vinsert128, target, target,
                                    high_half, MArgument(static_cast<u8>(1)));
          } else {
            fmt::println("{:cd}", insert_instr);
            TODO("impl");
          }
        } else if (input.ty == Type::Float32) {
          // vinsertps: count_s = 0 (take element 0 of the source scalar),
          // count_d = rev_indx (destination lane), zmask = 0000 (don't
          // clobber the other lanes we just copied in).
          u8 mapp = 0;
          switch (rev_indx) {
            case 1:
              mapp = 0b00'01'0000;
              break;
            case 2:
              mapp = 0b00'10'0000;
              break;
            case 3:
              mapp = 0b00'11'0000;
              break;
            default:
              fmt::print("{} {}", rev_indx, insert_instr);
              UNREACH();
          }
          res.result.emplace_back(
              X86Subtype::vinsertps, target, target,
              MArgument(input.reg.retype(helper_ty), helper_ty),
              MArgument{mapp});
        } else {
          TODO("impl");
        }
      }
      return true;
    }
    UNREACH();
  });
  rules.add(pm::ExtractElement, [](fir::Instr extract_instr, MatchResult &res,
                                   ExtraMatchData &data) {
    ASSERT(extract_instr->args.size() == 2);
    auto indx = extract_instr->args[1].as_constant()->as_int();
    if (extract_instr->args[0].get_type()->is_vec()) {
      auto input = valueToArg(extract_instr->args[0], res.result, data.alloc);
      ASSERT(extract_instr->args[1].is_constant() &&
             extract_instr->args[1].as_constant()->is_int());
      auto target = valueToArg(extract_instr.result(), res.result, data.alloc);
      auto get_xmm_version_ty = [](Type t) {
        if (t == Type::Int8) {
          return Type::Int8x16;
        }
        if (t == Type::Int16) {
          return Type::Int16x8;
        }
        if (t == Type::Float32) {
          return Type::Float32x4;
        }
        if (t == Type::Int32) {
          return Type::Int32x4;
        }
        if (t == Type::Float64) {
          return Type::Float64x2;
        }
        if (t == Type::Int64) {
          return Type::Int64x2;
        }
        fmt::println("{}", t);
        TODO("unreach?");
      };
      auto vec_ty = extract_instr->args[0].get_type()->as_vec();
      (void)vec_ty;
      // lane i of the FIR vector lives in lane i of the register
      auto rev_indx = indx;
      auto helper_ty = get_xmm_version_ty(target.ty);
      if (target.ty == Type::Int8 || target.ty == Type::Int16) {
        // pextrb / pextrw write a 32 bit register, the half of a 256 bit
        // vector the lane is in goes through vextract128 first
        ASSERT(input.isReg());
        const u32 per_half = 16 / get_size(target.ty);
        const auto lane = static_cast<u32>(rev_indx);
        auto src = MArgument(input.reg.retype(helper_ty), helper_ty);
        if (get_size(input.ty) == 32 && lane / per_half == 1) {
          src = MArgument(data.alloc.get_new_register(helper_ty), helper_ty);
          res.result.emplace_back(X86Subtype::vextract128, src, input,
                                  MArgument(static_cast<u8>(1)));
        }
        auto wide =
            MArgument(data.alloc.get_new_register(Type::Int32), Type::Int32);
        res.result.emplace_back(X86Subtype::vpextr, wide, src,
                                MArgument(static_cast<u8>(lane % per_half)));
        res.result.emplace_back(GConvSubtype::itrunc, target, wide);
        return true;
      }
      if (rev_indx == 0) {
        ASSERT(input.isReg())
        // last can just be moved
        res.result.emplace_back(
            GBaseSubtype::mov, target,
            MArgument(input.reg.retype(helper_ty), helper_ty));
      } else if (target.ty == Type::Float64 || target.ty == Type::Int64) {
        if (rev_indx == 0) {
          UNREACH();
        } else if (rev_indx == 1) {
          ASSERT(input.isReg())
          // seoncd to last can just be vpextr
          res.result.emplace_back(
              X86Subtype::vpextr, target,
              MArgument(input.reg.retype(helper_ty), helper_ty),
              MArgument(static_cast<u8>(1)));
        } else if (rev_indx == 2) {
          auto helper_ty = get_xmm_version_ty(target.ty);
          auto helper_reg = data.alloc.get_new_register(helper_ty);
          auto helper_arg = MArgument(helper_reg, helper_ty);
          res.result.emplace_back(X86Subtype::vextract128, helper_arg, input,
                                  MArgument(static_cast<u8>(1)));
          res.result.emplace_back(GBaseSubtype::mov, target, helper_arg);
        } else if (rev_indx == 3) {
          auto helper_ty = get_xmm_version_ty(target.ty);
          auto helper_reg = data.alloc.get_new_register(helper_ty);
          auto helper_arg = MArgument(helper_reg, helper_ty);
          res.result.emplace_back(X86Subtype::vextract128, helper_arg, input,
                                  MArgument(static_cast<u8>(1)));
          res.result.emplace_back(X86Subtype::vpextr, target, helper_arg,
                                  MArgument(static_cast<u8>(1)));
        } else {
          fmt::println("{}", extract_instr);
          TODO("Impl");
        }
      } else if (target.ty == Type::Float32 && rev_indx >= 4 && rev_indx < 8) {
        // upper 128 bit half into a helper, then pick the lane there
        auto helper_reg = data.alloc.get_new_register(helper_ty);
        auto helper_arg = MArgument(helper_reg, helper_ty);
        res.result.emplace_back(X86Subtype::vextract128, helper_arg, input,
                                MArgument(static_cast<u8>(1)));
        auto ext_out = MArgument{target.reg.retype(helper_ty), helper_ty};
        u8 mapp = static_cast<u8>(((rev_indx - 4) << 6) | 0b1110);
        res.result.emplace_back(GVecSubtype::vXor, ext_out, ext_out, ext_out);
        res.result.emplace_back(X86Subtype::vinsertps, ext_out, ext_out,
                                helper_arg, MArgument{mapp});
      } else if (target.ty == Type::Float32) {
        // the 128 bit forms read and write the xmm view
        auto ext_out =
            MArgument{target.reg.retype(Type::Float32x4), Type::Float32x4};
        auto input_x =
            MArgument{input.reg.retype(Type::Float32x4), Type::Float32x4};
        u8 mapp = 0;
        switch (rev_indx) {
          case 1:
            mapp = 0b01'00'1110;
            break;
          case 2:
            mapp = 0b10'00'1110;
            break;
          case 3:
            mapp = 0b11'00'1110;
            break;
          default:
            fmt::print("{} {}", rev_indx, extract_instr);
            UNREACH();
        }
        res.result.emplace_back(GVecSubtype::vXor, ext_out, ext_out, ext_out);
        res.result.emplace_back(X86Subtype::vinsertps, ext_out, ext_out,
                                input_x, MArgument{mapp});
      } else if (target.ty == Type::Int32 && rev_indx < 8) {
        ASSERT(input.isReg());
        if (rev_indx < 4) {
          res.result.emplace_back(
              X86Subtype::vpextr, target,
              MArgument(input.reg.retype(helper_ty), helper_ty),
              MArgument(static_cast<u8>(rev_indx)));
        } else {
          auto helper_reg = data.alloc.get_new_register(helper_ty);
          auto helper_arg = MArgument(helper_reg, helper_ty);
          res.result.emplace_back(X86Subtype::vextract128, helper_arg, input,
                                  MArgument(static_cast<u8>(1)));
          res.result.emplace_back(X86Subtype::vpextr, target, helper_arg,
                                  MArgument(static_cast<u8>(rev_indx - 4)));
        }
      } else {
        fmt::println("{} {}", target.ty, extract_instr);
        TODO("impl");
      }
      return true;
    }
    UNREACH();
  });
}

void intrin_rules(RuleSet &rules) {
  rules.add(pm::FRound,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
              auto arg = valueToArg(instr->args[0], res.result, data.alloc);
              res.result.emplace_back(X86Subtype::vround, res_reg, arg, arg,
                                      MArgument(static_cast<u8>(0b00)));
              return true;
            });
  rules.add(pm::IsConstant,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
              res.result.emplace_back(GArithSubtype::lxor2, res_reg, res_reg);
              return true;
            });
  rules.add(pm::FCeil,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
              auto arg = valueToArg(instr->args[0], res.result, data.alloc);
              res.result.emplace_back(X86Subtype::vround, res_reg, arg, arg,
                                      MArgument(static_cast<u8>(0b10)));
              return true;
            });
  rules.add(pm::FFloor,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
              auto arg = valueToArg(instr->args[0], res.result, data.alloc);
              res.result.emplace_back(X86Subtype::vround, res_reg, arg, arg,
                                      MArgument(static_cast<u8>(0b01)));
              return true;
            });
  rules.add(pm::FTrunc,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
              auto arg = valueToArg(instr->args[0], res.result, data.alloc);
              res.result.emplace_back(X86Subtype::vround, res_reg, arg, arg,
                                      MArgument(static_cast<u8>(0b11)));
              return true;
            });
  // minnum / maxnum: the hardware instruction returns its second operand when
  // either one is NaN, but a NaN operand has to lose against a number
  static constexpr auto fminmax = [](GVecSubtype op, fir::Instr instr,
                                     MatchResult &res, ExtraMatchData &data) {
    auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
    auto a = valueToArg(instr->args[0], res.result, data.alloc);
    auto b = valueToArg(instr->args[1], res.result, data.alloc);
    if (data.config.optim.fltOpt.no_nans) {
      res.result.emplace_back(op, res_reg, a, b);
      return true;
    }
    auto raw = MArgument(data.alloc.get_new_register(res_reg.ty), res_reg.ty);
    res.result.emplace_back(op, raw, a, b);
    auto mask_ty = res_reg.ty;
    if (res_reg.ty == Type::Float32) {
      mask_ty = Type::Int32x4;
    } else if (res_reg.ty == Type::Float64) {
      mask_ty = Type::Int64x2;
    }
    auto b_is_nan = MArgument(data.alloc.get_new_register(mask_ty), mask_ty);
    // unordered with itself, 3 is the unordered predicate of vcmp
    res.result.emplace_back(X86Subtype::vcmp, b_is_nan, b, b,
                            MArgument(static_cast<u8>(3)));
    res.result.emplace_back(X86Subtype::vblendv, res_reg, raw, a, b_is_nan);
    return true;
  };
  rules.add(pm::FMin,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              return fminmax(GVecSubtype::fMin, instr, res, data);
            });
  rules.add(pm::FMax,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              return fminmax(GVecSubtype::fMax, instr, res, data);
            });
  rules.add(
      pm::SMax, [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
        auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
        auto a = valueToArg(instr->args[0], res.result, data.alloc);
        auto b = valueToArg(instr->args[1], res.result, data.alloc);
        if (res_reg.is_vec_reg()) {
          res.result.emplace_back(GVecSubtype::vSMax, res_reg, a, b);
          return true;
        }
        res.result.emplace_back(GBaseSubtype::mov, res_reg, b);
        res.result.emplace_back(GCMovSubtype::cmov_slt, res_reg, a, res_reg, a);
        return true;
      });
  rules.add(
      pm::UMax, [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
        auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
        auto a = valueToArg(instr->args[0], res.result, data.alloc);
        auto b = valueToArg(instr->args[1], res.result, data.alloc);
        if (res_reg.is_vec_reg()) {
          res.result.emplace_back(GVecSubtype::vUMax, res_reg, a, b);
          return true;
        }
        res.result.emplace_back(GBaseSubtype::mov, res_reg, b);
        res.result.emplace_back(GCMovSubtype::cmov_ult, res_reg, a, res_reg, a);
        return true;
      });
  rules.add(
      pm::SMin, [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
        auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
        auto a = valueToArg(instr->args[0], res.result, data.alloc);
        auto b = valueToArg(instr->args[1], res.result, data.alloc);
        if (res_reg.is_vec_reg()) {
          res.result.emplace_back(GVecSubtype::vSMin, res_reg, a, b);
          return true;
        }
        res.result.emplace_back(GBaseSubtype::mov, res_reg, b);
        res.result.emplace_back(GCMovSubtype::cmov_sgt, res_reg, a, res_reg, a);
        return true;
      });
  rules.add(
      pm::UMin, [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
        auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
        auto a = valueToArg(instr->args[0], res.result, data.alloc);
        auto b = valueToArg(instr->args[1], res.result, data.alloc);
        if (res_reg.is_vec_reg()) {
          res.result.emplace_back(GVecSubtype::vUMin, res_reg, a, b);
          return true;
        }
        res.result.emplace_back(GBaseSubtype::mov, res_reg, b);
        res.result.emplace_back(GCMovSubtype::cmov_ugt, res_reg, a, res_reg, a);
        return true;
      });
  rules.add(
      pm::Ctlz, [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
        auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
        auto arg = valueToArg(instr->args[0], res.result, data.alloc);
        if (arg.isImm()) {
          // no immediate source operand
          res.result.emplace_back(GBaseSubtype::mov, res_reg, arg);
          arg = res_reg;
        }
        if (res_reg.ty == Type::Int8) {
          // no byte form, count in 32 bits and drop the 24 extra zeros
          auto wide =
              MArgument(data.alloc.get_new_register(Type::Int32), Type::Int32);
          res.result.emplace_back(GConvSubtype::mov_zx, wide, arg);
          res.result.emplace_back(X86Subtype::lzcnt, wide, wide);
          res.result.emplace_back(GArithSubtype::sub2, wide,
                                  MArgument::Int(24, Type::Int32));
          res.result.emplace_back(GConvSubtype::itrunc, res_reg, wide);
          return true;
        }
        res.result.emplace_back(X86Subtype::lzcnt, res_reg, arg);
        return true;
      });
  rules.add(
      pm::Cttz, [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
        auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
        auto arg = valueToArg(instr->args[0], res.result, data.alloc);
        if (arg.isImm()) {
          // no immediate source operand
          res.result.emplace_back(GBaseSubtype::mov, res_reg, arg);
          arg = res_reg;
        }
        if (res_reg.ty == Type::Int8) {
          // no byte form, a set bit 8 makes a zero input count 8
          auto wide =
              MArgument(data.alloc.get_new_register(Type::Int32), Type::Int32);
          res.result.emplace_back(GConvSubtype::mov_zx, wide, arg);
          res.result.emplace_back(GArithSubtype::lor2, wide,
                                  MArgument::Int(0x100, Type::Int32));
          res.result.emplace_back(X86Subtype::tzcnt, wide, wide);
          res.result.emplace_back(GConvSubtype::itrunc, res_reg, wide);
          return true;
        }
        res.result.emplace_back(X86Subtype::tzcnt, res_reg, arg);
        return true;
      });
  rules.add(pm::Bswap,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
              auto arg = valueToArg(instr->args[0], res.result, data.alloc);
              res.result.emplace_back(X86Subtype::bswap, res_reg, arg);
              return true;
            });
  rules.add(pm::PopCnt, [](fir::Instr instr, MatchResult &res,
                           ExtraMatchData &data) {
    auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
    auto arg = valueToArgPosMem(instr->args[0], res.result, data.alloc, instr);
    if (arg.isImm()) {
      // no immediate source operand
      res.result.emplace_back(GBaseSubtype::mov, res_reg, arg);
      arg = res_reg;
    }
    res.result.emplace_back(X86Subtype::popcnt, res_reg, arg);
    return true;
  });
  rules.add(pm::Abs, [](fir::Instr instr, MatchResult &res,
                        ExtraMatchData &data) {
    auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
    auto arg = valueToArg(instr->args[0], res.result, data.alloc);
    if (instr->result_type()->is_vec()) {
      const bool i64_lanes =
          res_reg.ty == Type::Int64x2 || res_reg.ty == Type::Int64x4;
      if (i64_lanes && !data.config.target.features.avx512vl) {
        // vpabsq is evex only: blend x and 0 - x on the sign of x
        auto zero =
            MArgument(data.alloc.get_new_register(res_reg.ty), res_reg.ty);
        auto neg =
            MArgument(data.alloc.get_new_register(res_reg.ty), res_reg.ty);
        res.result.emplace_back(GVecSubtype::vXor, zero, arg, arg);
        res.result.emplace_back(GVecSubtype::vsub, neg, zero, arg);
        res.result.emplace_back(X86Subtype::vblendv, res_reg, arg, neg, arg);
        return true;
      }
      res.result.emplace_back(X86Subtype::vpabs, res_reg, arg);
      return true;
    }
    if (arg.ty == Type::Int8) {
      // there is no 8 bit cmov, take the low byte of the 32 bit abs
      auto wide =
          MArgument(data.alloc.get_new_register(Type::Int32), Type::Int32);
      auto wide_abs =
          MArgument(data.alloc.get_new_register(Type::Int32), Type::Int32);
      auto low = wide_abs;
      low.ty = Type::Int8;
      low.reg.ty = Type::Int8;
      res.result.emplace_back(GConvSubtype::mov_sx, wide, arg);
      res.result.emplace_back(GArithSubtype::abs, wide_abs, wide);
      res.result.emplace_back(GBaseSubtype::mov, res_reg, low);
      return true;
    }
    res.result.emplace_back(GArithSubtype::abs, res_reg, arg);
    return true;
  });
  rules.add(pm::FAbs, [](fir::Instr instr, MatchResult &res,
                         ExtraMatchData &data) {
    auto res_reg = valueToArg(instr.result(), res.result, data.alloc);
    auto arg = valueToArg(instr->args[0], res.result, data.alloc);
    if (arg.ty == Type::Float32) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg, arg);
      res.result.emplace_back(
          GVecSubtype::vAnd, res_reg, res_reg,
          MArgument(std::bit_cast<f32>(static_cast<u32>(0x7fffffff))));
    } else if (arg.ty == Type::Float64) {
      res.result.emplace_back(GBaseSubtype::mov, res_reg, arg);
      res.result.emplace_back(
          GVecSubtype::vAnd, res_reg, res_reg,
          MArgument(std::bit_cast<f64>(static_cast<u64>(0x7fffffffffffffff))));

    } else {
      TODO("impl");
    }
    return true;
  });
  rules.add(pm::VaStart,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              setup_va_start(instr, res, data);
              return true;
            });
  rules.add(pm::VaEnd,
            [](fir::Instr instr, MatchResult &res, ExtraMatchData &data) {
              setup_va_end(instr, res, data);
              return true;
            });
}
}  // namespace

const RuleSet &builtin_rules() {
  static const RuleSet rules = [] {
    RuleSet res;
    memory_rules(res);
    cjmp_rules(res);
    move_rules(res);
    arith_rules(res);
    intrin_rules(res);
    vec_rules(res);
    base_rules(res);
    return res;
  }();
  return rules;
}
}  // namespace foptim::fmir
