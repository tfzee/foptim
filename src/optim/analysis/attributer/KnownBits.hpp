#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include <bit>
#include <optional>

#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "utils/APInt.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

class KnownBits;

static KnownBits computeForAddCarry(const KnownBits &LHS, const KnownBits &RHS,
                                    bool CarryZero, bool CarryOne);
static KnownBits computeMul(const KnownBits &LHS, const KnownBits &RHS);
static KnownBits abs_known_bits(const KnownBits &input);

// Bits of one value, known_zero and known_one never share a set bit. Both have
// the bitwidth of the type of the value.
class KnownBits final : public AttributeAnalysis,
                        public AttributeInfo<KnownBits> {
 public:
  static AttributeKey Key;
  KnownBits() = default;
  ~KnownBits() override = default;

  APInt known_zero{};
  APInt known_one{};

  [[nodiscard]] static u32 width_of(fir::TypeR t) {
    const u32 w = t->get_bitwidth();
    return w == 0 ? 1 : w;
  }
  [[nodiscard]] u32 width() const { return known_one.bits(); }

  [[nodiscard]] static KnownBits unknown(u32 bits) {
    KnownBits res;
    res.known_zero = APInt::zero(bits);
    res.known_one = APInt::zero(bits);
    return res;
  }

  void init() override {
    const u32 w = width_of(associatedValue.get_type());
    known_zero = APInt::zero(w);
    known_one = APInt::zero(w);
  }

  [[nodiscard]] static KnownBits from_umin_umax(const APInt &umin,
                                                const APInt &umax) {
    KnownBits bits = unknown(umin.bits());
    const APInt diff = umin ^ umax;
    if (diff.is_zero()) {
      bits.known_one = umin;
      bits.known_zero = ~umin;
    } else {
      // everything above the highest differing bit is shared
      const APInt mask = APInt::high_bits(umin.bits(), diff.clz());
      bits.known_one = umin & mask;
      bits.known_zero = (~umin) & mask;
    }
    return bits;
  }

  // all bits are known
  [[nodiscard]] bool is_constant() const {
    return (known_zero | known_one).is_all_ones();
  }

  bool materialize_impl(fir::Context &ctx,
                        const AttributerManager &m) override {
    (void)ctx;
    bool modified = false;
    if (!associatedValue.is_instr()) {
      return false;
    }
    auto instr = associatedValue.as_instr();
    if (instr->is(fir::BinaryInstrSubType::IntAdd) ||
        instr->is(fir::BinaryInstrSubType::PtrAdd)) {
      const auto *known_arg0_bits = m.get_analysis<KnownBits>(instr->args[0]);
      const auto *known_arg1_bits = m.get_analysis<KnownBits>(instr->args[1]);
      if ((known_arg0_bits == nullptr) || (known_arg1_bits == nullptr)) {
        return false;
      }
      const u32 bitwidth = instr->result_type()->get_bitwidth();
      if (known_arg0_bits->width() != bitwidth ||
          known_arg1_bits->width() != bitwidth) {
        return false;
      }
      // the sums are computed one bit wider so they cannot wrap
      const u32 wide = bitwidth + 1;
      {
        const APInt biggest_val =
            known_arg0_bits->get_signed_max_value().sext(wide) +
            known_arg1_bits->get_signed_max_value().sext(wide);
        const APInt smallest_val =
            known_arg0_bits->get_signed_min_value().sext(wide) +
            known_arg1_bits->get_signed_min_value().sext(wide);
        const APInt signed_max = APInt::signed_max(bitwidth).sext(wide);
        const APInt signed_min = APInt::signed_min(bitwidth).sext(wide);
        if (biggest_val.slt(signed_max) && smallest_val.sge(signed_min) &&
            !instr->NSW) {
          instr->NSW = true;
          modified = true;
        }
      }
      {
        const APInt biggest_val =
            known_arg0_bits->get_unsigned_max_value().zext(wide) +
            known_arg1_bits->get_unsigned_max_value().zext(wide);
        const APInt smallest_val =
            known_arg0_bits->get_unsigned_min_value().zext(wide) +
            known_arg1_bits->get_unsigned_min_value().zext(wide);
        const APInt unsigned_max = APInt::all_ones(bitwidth).zext(wide);
        if (biggest_val.ult(unsigned_max) && !smallest_val.is_zero() &&
            !instr->NUW) {
          instr->NUW = true;
          modified = true;
        }
      }
    }
    return modified;
  }

  enum BitInfo {
    Unknown = 0,
    KnownZero = 1,
    KnownOne = 2,
  };

  [[nodiscard]] BitInfo msb_info() const {
    const u32 msb = width() - 1;
    const bool is_one = known_one.bit(msb);
    const bool is_zero = known_zero.bit(msb);
    ASSERT(!is_one || !is_zero);
    if (is_one) {
      return BitInfo::KnownOne;
    }
    if (is_zero) {
      return BitInfo::KnownZero;
    }
    return BitInfo::Unknown;
  }

  // the value is a power of two of a fully known value, returns its log2
  [[nodiscard]] static std::optional<u32> known_log2(const KnownBits &b) {
    if (b.is_constant() && b.known_one.is_pow2()) {
      return b.known_one.log2();
    }
    return {};
  }

  // result of an operation that has a known shift amount below the width
  [[nodiscard]] static std::optional<u32> known_shift(const KnownBits &b,
                                                      u32 width) {
    if (b.is_constant() && b.known_one.fits_u64() &&
        b.known_one.to_u64() < width) {
      return static_cast<u32>(b.known_one.to_u64());
    }
    return {};
  }

  // the smallest shift amount that is not above the width
  [[nodiscard]] static u32 min_shift(const KnownBits &b, u32 width) {
    const APInt &mn = b.get_unsigned_min_value();
    if (!mn.fits_u64() || mn.to_u64() > width) {
      return width;
    }
    return static_cast<u32>(mn.to_u64());
  }

  Result update_impl_instr(AttributerManager &m, Worklist &worklist) {
    const u32 W = width();
    // lanes of vectors do not behave like one integer
    if (associatedValue.get_type()->is_vec()) {
      return Result::Fixed;
    }
    APInt new_known_one = known_one;
    APInt new_known_zero = known_zero;
    auto instr = associatedValue.as_instr();
    auto arg_bits = [&](u32 i) {
      return m.get_or_create_analysis<KnownBits>(instr->args[i], &worklist);
    };
    if (instr->is(fir::InstrType::ZExt)) {
      const auto *known_arg_bits = arg_bits(0);
      const u32 old_bitwidth = known_arg_bits->width();
      new_known_one = known_arg_bits->known_one.zext_or_trunc(W);
      new_known_zero = known_arg_bits->known_zero.zext_or_trunc(W);
      if (W > old_bitwidth) {
        new_known_zero |= APInt::high_bits(W, W - old_bitwidth);
      }
    } else if (instr->is(fir::InstrType::ICmp) ||
               instr->is(fir::InstrType::FCmp)) {
      new_known_one = APInt::zero(W);
      new_known_zero = APInt::high_bits(W, W - 1);
    } else if (instr->is(fir::InstrType::Intrinsic)) {
      switch (static_cast<fir::IntrinsicSubType>(instr->subtype)) {
        case fir::IntrinsicSubType::IsConstant: {
          const auto *known_arg_bits = arg_bits(0);
          const bool all_known = known_arg_bits->is_constant();
          new_known_one = APInt(W, u64{all_known});
          new_known_zero = APInt::zero(W);
          break;
        }
        case fir::IntrinsicSubType::Abs: {
          const auto *known_arg_bits = arg_bits(0);
          auto res = abs_known_bits(*known_arg_bits);
          new_known_one = res.known_one;
          new_known_zero = res.known_zero;
          break;
        }
        case fir::IntrinsicSubType::UMax:
        case fir::IntrinsicSubType::UMin:
        case fir::IntrinsicSubType::SMax:
        case fir::IntrinsicSubType::SMin: {
          const auto sub = static_cast<fir::IntrinsicSubType>(instr->subtype);
          const auto *a = arg_bits(0);
          const auto *b = arg_bits(1);
          const bool is_signed = sub == fir::IntrinsicSubType::SMax ||
                                 sub == fir::IntrinsicSubType::SMin;
          const bool is_max = sub == fir::IntrinsicSubType::UMax ||
                              sub == fir::IntrinsicSubType::SMax;
          const APInt a_min = is_signed ? a->get_signed_min_value()
                                        : a->get_unsigned_min_value();
          const APInt a_max = is_signed ? a->get_signed_max_value()
                                        : a->get_unsigned_max_value();
          const APInt b_min = is_signed ? b->get_signed_min_value()
                                        : b->get_unsigned_min_value();
          const APInt b_max = is_signed ? b->get_signed_max_value()
                                        : b->get_unsigned_max_value();
          auto ge = [&](const APInt &x, const APInt &y) {
            return is_signed ? x.sge(y) : x.uge(y);
          };
          // one operand is always the result when the ranges do not overlap
          const bool take_a = is_max ? ge(a_min, b_max) : ge(b_min, a_max);
          const bool take_b = is_max ? ge(b_min, a_max) : ge(a_min, b_max);
          if (take_a) {
            new_known_one = a->known_one;
            new_known_zero = a->known_zero;
          } else if (take_b) {
            new_known_one = b->known_one;
            new_known_zero = b->known_zero;
          } else {
            new_known_one = a->known_one & b->known_one;
            new_known_zero = a->known_zero & b->known_zero;
          }
          break;
        }
        case fir::IntrinsicSubType::CTLZ:
        case fir::IntrinsicSubType::CTTZ:
        case fir::IntrinsicSubType::PopCnt: {
          const auto sub = static_cast<fir::IntrinsicSubType>(instr->subtype);
          const auto *a = arg_bits(0);
          const u32 aw = a->width();
          if (a->is_constant()) {
            const APInt &v = a->known_one;
            const u32 r = sub == fir::IntrinsicSubType::CTLZ   ? v.clz()
                          : sub == fir::IntrinsicSubType::CTTZ ? v.ctz()
                                                               : v.popcount();
            new_known_one = APInt(W, u64{r});
            new_known_zero = ~new_known_one;
          } else if (sub == fir::IntrinsicSubType::PopCnt) {
            const APInt lo(W, u64{a->known_one.popcount()});
            const APInt hi(W, u64{aw - a->known_zero.popcount()});
            KnownBits res = KnownBits::from_umin_umax(lo, hi);
            new_known_zero = res.known_zero;
            new_known_one = res.known_one;
          } else {
            // the result is at most the width of the argument
            const u32 needed = std::bit_width(aw);
            new_known_one = APInt::zero(W);
            new_known_zero =
                needed >= W ? APInt::zero(W) : APInt::high_bits(W, W - needed);
          }
          break;
        }
        case fir::IntrinsicSubType::FAbs:
        case fir::IntrinsicSubType::FMin:
        case fir::IntrinsicSubType::FMax:
        case fir::IntrinsicSubType::FMinimum:
        case fir::IntrinsicSubType::FMaximum:
        case fir::IntrinsicSubType::FRound:
        case fir::IntrinsicSubType::FCeil:
        case fir::IntrinsicSubType::FFloor:
        case fir::IntrinsicSubType::FTrunc:
          fmt::println("BITS KNOWN {}", *this);
          fmt::println("TODO: ATTRIB KNOWN BITS intrinsic OP {}",
                       associatedValue.as_instr());
          TODO("impl");
        case fir::IntrinsicSubType::Bswap: {
          const auto *known_arg_bits = arg_bits(0);
          // the bytes move as a whole
          new_known_one = APInt::zero(W);
          new_known_zero = APInt::zero(W);
          for (u32 byte = 0; byte < W / 8; byte++) {
            const u32 dst = W - 8 - 8 * byte;
            const APInt one_byte =
                known_arg_bits->known_one.lshr(8 * byte).trunc(8).zext(W);
            const APInt zero_byte =
                known_arg_bits->known_zero.lshr(8 * byte).trunc(8).zext(W);
            new_known_one |= one_byte.shl(dst);
            new_known_zero |= zero_byte.shl(dst);
          }
          break;
        }
        case fir::IntrinsicSubType::INVALID:
        case fir::IntrinsicSubType::VA_start:
        case fir::IntrinsicSubType::VA_end:
        case fir::IntrinsicSubType::Memcmp:
        case fir::IntrinsicSubType::MaskedLoad:
        case fir::IntrinsicSubType::Gather:
          // an arbitrary int
          break;
        case fir::IntrinsicSubType::Memset:
        case fir::IntrinsicSubType::Memcpy:
        case fir::IntrinsicSubType::Memmove:
        case fir::IntrinsicSubType::MaskedStore:
          TODO("UNREACH");
      }
    } else if (instr->is(fir::InstrType::ITrunc)) {
      const auto *known_arg_bits = arg_bits(0);
      new_known_one = known_arg_bits->known_one.zext_or_trunc(W);
      new_known_zero = known_arg_bits->known_zero.zext_or_trunc(W);
    } else if (instr->is(fir::InstrType::AllocaInstr)) {
      // TODO: idk how to handle this best
      new_known_one = APInt::zero(W);
      new_known_zero = APInt::zero(W);
    } else if (instr->is(fir::InstrType::SelectInstr)) {
      const auto *known_arg1_bits = arg_bits(1);
      const auto *known_arg2_bits = arg_bits(2);
      new_known_one = known_arg1_bits->known_one & known_arg2_bits->known_one;
      new_known_zero =
          known_arg1_bits->known_zero & known_arg2_bits->known_zero;
    } else if (instr->is(fir::InstrType::SExt)) {
      const auto *known_arg_bits = arg_bits(0);
      const u32 old_bitwidth = known_arg_bits->width();
      new_known_one = known_arg_bits->known_one.zext_or_trunc(W);
      new_known_zero = known_arg_bits->known_zero.zext_or_trunc(W);
      if (W > old_bitwidth) {
        const auto old_msb = known_arg_bits->msb_info();
        if (old_msb == KnownOne) {
          new_known_one |= APInt::high_bits(W, W - old_bitwidth);
        } else if (old_msb == KnownZero) {
          new_known_zero |= APInt::high_bits(W, W - old_bitwidth);
        }
      }
    } else if (instr->is(fir::InstrType::Conversion)) {
      switch (static_cast<fir::ConversionSubType>(instr->subtype)) {
        case fir::ConversionSubType::PtrToInt:
        case fir::ConversionSubType::IntToPtr: {
          const auto *a = arg_bits(0);
          const u32 aw = a->width();
          new_known_one = a->known_one.zext_or_trunc(W);
          new_known_zero = a->known_zero.zext_or_trunc(W);
          if (W > aw) {
            new_known_zero |= APInt::high_bits(W, W - aw);
          }
        } break;
        case fir::ConversionSubType::BitCast:
        case fir::ConversionSubType::INVALID:
        case fir::ConversionSubType::FPEXT:
        case fir::ConversionSubType::FPTRUNC:
        case fir::ConversionSubType::FPTOUI:
        case fir::ConversionSubType::FPTOSI:
        case fir::ConversionSubType::UITOFP:
        case fir::ConversionSubType::SITOFP:
          break;
      }
    } else if (instr->is(fir::InstrType::UnaryInstr)) {
      const auto *a = arg_bits(0);
      switch (static_cast<fir::UnaryInstrSubType>(instr->subtype)) {
        case fir::UnaryInstrSubType::INVALID:
          TODO("UNREACH");
        case fir::UnaryInstrSubType::FloatSqrt:
        case fir::UnaryInstrSubType::FloatNeg:
        case fir::UnaryInstrSubType::IntNeg:
          break;
        case fir::UnaryInstrSubType::Not:
          new_known_zero = a->known_one;
          new_known_one = a->known_zero;
          break;
      }
    } else if (instr->is(fir::InstrType::BinaryInstr)) {
      const auto *a = arg_bits(0);
      const auto *b = arg_bits(1);
      const auto sub = static_cast<fir::BinaryInstrSubType>(instr->subtype);
      if (a->width() != W || b->width() != W) {
        // mixed widths, nothing to say
      } else if (sub == fir::BinaryInstrSubType::IntAdd ||
                 sub == fir::BinaryInstrSubType::PtrAdd) {
        auto res = computeForAddCarry(*a, *b, true, false);
        new_known_one = res.known_one;
        new_known_zero = res.known_zero;
      } else if (sub == fir::BinaryInstrSubType::IntMul) {
        auto res = computeMul(*a, *b);
        new_known_one = res.known_one;
        new_known_zero = res.known_zero;
      } else if (sub == fir::BinaryInstrSubType::IntSub) {
        auto flippedB = KnownBits::unknown(W);
        flippedB.known_one = b->known_zero;
        flippedB.known_zero = b->known_one;
        auto res = computeForAddCarry(*a, flippedB, false, true);
        new_known_one = res.known_one;
        new_known_zero = res.known_zero;
      } else if (sub == fir::BinaryInstrSubType::IntURem) {
        new_known_one = APInt::zero(W);
        new_known_zero = APInt::zero(W);
        // if a is always smaller than b the urem does nothing
        if (b->get_unsigned_min_value().ugt(a->get_unsigned_max_value())) {
          new_known_one = a->known_one;
          new_known_zero = a->known_zero;
        }
      } else if (sub == fir::BinaryInstrSubType::IntUDiv) {
        if (auto log2 = known_log2(*b)) {
          new_known_one = a->known_one.lshr(*log2);
          new_known_zero = a->known_zero.lshr(*log2) |
                           APInt::high_bits(W, std::min(*log2, W));
        }
      } else if (sub == fir::BinaryInstrSubType::IntSDiv) {
        // only a non negative dividend rounds like a shift
        if (auto log2 = known_log2(*b); log2 && a->msb_info() == KnownZero) {
          new_known_one = a->known_one.lshr(*log2);
          new_known_zero = a->known_zero.lshr(*log2) |
                           APInt::high_bits(W, std::min(*log2, W));
        }
      } else if (sub == fir::BinaryInstrSubType::Xor) {
        new_known_one =
            (a->known_zero & b->known_one) | (a->known_one & b->known_zero);
        new_known_zero =
            (a->known_zero & b->known_zero) | (a->known_one & b->known_one);
      } else if (sub == fir::BinaryInstrSubType::And) {
        new_known_one = a->known_one & b->known_one;
        new_known_zero = a->known_zero | b->known_zero;
      } else if (sub == fir::BinaryInstrSubType::Or) {
        new_known_one = a->known_one | b->known_one;
        new_known_zero = a->known_zero & b->known_zero;
      } else if (sub == fir::BinaryInstrSubType::Shr) {
        if (auto amt = known_shift(*b, W)) {
          new_known_one = a->known_one.lshr(*amt);
          new_known_zero = a->known_zero.lshr(*amt) | APInt::high_bits(W, *amt);
        } else {
          // the top n bits are zero after the shift by atleast n
          new_known_one = APInt::zero(W);
          new_known_zero = APInt::high_bits(W, min_shift(*b, W));
        }
      } else if (sub == fir::BinaryInstrSubType::Shl) {
        if (auto amt = known_shift(*b, W)) {
          new_known_one = a->known_one.shl(*amt);
          new_known_zero = a->known_zero.shl(*amt) | APInt::low_bits(W, *amt);
        } else {
          // the low n bits are zero after the shift by atleast n
          new_known_one = APInt::zero(W);
          new_known_zero = APInt::low_bits(W, min_shift(*b, W));
        }
      } else if (sub == fir::BinaryInstrSubType::IntSRem) {
        new_known_one = APInt::zero(W);
        new_known_zero = APInt::zero(W);
        // constant divisor c: |a srem c| < c and the result has the sign of
        // a, so for a non negative a and c > 0 the result lies in [0, c-1]
        if (b->is_constant() && b->msb_info() == KnownZero &&
            !b->known_one.is_zero() && a->msb_info() == KnownZero) {
          const u32 res_bits = (b->known_one - APInt(W, u64{1})).active_bits();
          new_known_zero = APInt::high_bits(W, W - std::min(res_bits, W));
        }
      } else if (sub == fir::BinaryInstrSubType::AShr) {
        if (auto amt = known_shift(*b, W)) {
          // the sign fill of the arithmetic shift is right for both masks
          new_known_one = a->known_one.ashr(*amt);
          new_known_zero = a->known_zero.ashr(*amt);
        } else {
          // we don't know the shifted bits, but the sign propagates
          new_known_one = APInt::zero(W);
          new_known_zero = APInt::zero(W);
          const APInt sign_extension = APInt::high_bits(W, min_shift(*b, W));
          const auto msb = a->msb_info();
          if (msb == KnownOne) {
            new_known_one |= sign_extension;
          } else if (msb == KnownZero) {
            new_known_zero |= sign_extension;
          }
        }
      } else {
        fmt::println("BITS KNOWN {}", *this);
        fmt::println("TODO: ATTRIB KNOWN BITS BIINARY OP {}",
                     associatedValue.as_instr());
      }
    } else if (instr->is(fir::InstrType::VectorInstr)) {
      switch (static_cast<fir::VectorISubType>(instr->subtype)) {
        case fir::VectorISubType::INVALID:
        case fir::VectorISubType::Broadcast:
        case fir::VectorISubType::Shuffle:
        case fir::VectorISubType::Concat:
        case fir::VectorISubType::ExtractHigh:
        case fir::VectorISubType::ExtractLow:
          new_known_one = APInt::zero(W);
          new_known_zero = APInt::zero(W);
          break;
      }
    } else if (instr->is(fir::InstrType::ReduceInstr)) {
      new_known_one = APInt::zero(W);
      new_known_zero = APInt::zero(W);
    } else if (instr->is(fir::InstrType::ExtractElement)) {
      // TODO: can propagate in certain conditions (but then we prob removing
      // this extract value later one hm)
    } else if (instr->is(fir::InstrType::LoadInstr)) {
    } else if (instr->is(fir::InstrType::AtomicRMW)) {
      // returns old value so same as load
    } else if (instr->is(fir::InstrType::CallInstr)) {
      // TODO: handle special builtin call instrs
    } else {
      fmt::println("BITS KNOWN {}", *this);
      fmt::println("TODO: ATTRIB KNOWN BITS {}", associatedValue.as_instr());
    }

    return commit(std::move(new_known_one), std::move(new_known_zero));
  }

  Result commit(APInt new_known_one, APInt new_known_zero) {
    if (new_known_one != known_one || new_known_zero != known_zero) {
      if (!(new_known_zero & new_known_one).is_zero()) {
        if (associatedValue.is_instr()) {
          fmt::println(
              "Failed known bits on {:c}\n it marked bits as both 1 and 0",
              associatedValue.as_instr());
        } else {
          fmt::println(
              "Failed known bits on {:c}\n it marked bits as both 1 and 0",
              associatedValue);
        }
        fmt::println("{} {}", new_known_one.to_string(false, 16),
                     new_known_zero.to_string(false, 16));
        TODO("Failed");
      }
      known_zero = std::move(new_known_zero);
      known_one = std::move(new_known_one);
      return Result::Changed;
    }
    return Result::Fixed;
  }

  Result update_impl_constant() {
    const u32 W = width();
    APInt new_known_one = known_one;
    APInt new_known_zero = known_zero;

    auto constant = associatedValue.as_constant();
    switch (constant->ty) {
      case fir::ConstantType::PoisonValue:
        new_known_one = APInt::zero(W);
        new_known_zero = APInt::all_ones(W);
        break;
      case fir::ConstantType::IntValue: {
        new_known_one = constant->as_apint().zext_or_trunc(W);
        new_known_zero = ~new_known_one;
      } break;
      case fir::ConstantType::FloatValue: {
        auto bitwidth = constant->get_type()->as_float();
        if (bitwidth == 64) {
          new_known_one = APInt(W, std::bit_cast<u64>(constant->as_f64()));
          new_known_zero = ~new_known_one;
        } else if (bitwidth == 32) {
          new_known_one = APInt(W, u64{std::bit_cast<u32>(constant->as_f32())});
          new_known_zero = ~new_known_one;
        }
      } break;
      case fir::ConstantType::NullPtr: {
        new_known_one = APInt::zero(W);
        new_known_zero = APInt::all_ones(W);
      } break;
      case fir::ConstantType::FuncPtr: {
        auto min_align = constant->as_func().func->attribs.min_align;
        if (min_align > 0) {
          new_known_one = APInt::zero(W);
          new_known_zero = APInt(W, static_cast<u64>(min_align) - 1);
        }
      } break;
      case fir::ConstantType::GlobalPtr: {
        auto min_align = constant->as_global()->min_align;
        if (min_align > 0) {
          new_known_one = APInt::zero(W);
          new_known_zero = APInt(W, static_cast<u64>(min_align) - 1);
        }
      } break;
      case fir::ConstantType::VectorValue:
        break;
    }

    if (new_known_one != known_one || new_known_zero != known_zero) {
      known_zero = std::move(new_known_zero);
      known_one = std::move(new_known_one);
      return Result::Changed;
    }
    return Result::Fixed;
  }

  Result update_impl(AttributerManager &m, Worklist &worklist) override {
    if (associatedValue.is_instr()) {
      return update_impl_instr(m, worklist);
    }
    if (associatedValue.is_constant()) {
      return update_impl_constant();
    }
    if (associatedValue.is_bb_arg()) {
      const u32 W = width();
      APInt new_known_one = APInt::zero(W);
      APInt new_known_zero = APInt::zero(W);
      auto bb_arg = associatedValue.as_bb_arg();
      auto parent = bb_arg->get_parent();
      auto bb_arg_id = bb_arg->get_parent()->get_arg_id(bb_arg);
      if (parent->get_n_uses() > 0) {
        new_known_one = APInt::all_ones(W);
        new_known_zero = APInt::all_ones(W);
        for (auto use : parent->uses) {
          ASSERT(use.type == fir::UseType::BB);
          auto input_value = use.user->bbs[use.argId].args[bb_arg_id];
          const auto *known_arg_bits =
              m.get_or_create_analysis<KnownBits>(input_value, &worklist);
          if (known_arg_bits->width() != W) {
            // a passed value of another width, nothing known
            new_known_one = APInt::zero(W);
            new_known_zero = APInt::zero(W);
            break;
          }
          new_known_one = new_known_one & known_arg_bits->known_one;
          new_known_zero = new_known_zero & known_arg_bits->known_zero;
          // early break since its dynamic value anyway
          if (new_known_zero.is_zero() && new_known_one.is_zero()) {
            break;
          }
        }
        ASSERT((new_known_one & new_known_zero).is_zero());
      }
      if (new_known_one != known_one || new_known_zero != known_zero) {
        known_zero = std::move(new_known_zero);
        known_one = std::move(new_known_one);
        return Result::Changed;
      }
    } else {
      fmt::println("TODO: ATTRIB KNOWN BITS {}", associatedValue);
    }
    return Result::Fixed;
  }

  [[nodiscard]] APInt get_unsigned_max_value() const { return ~known_zero; }
  [[nodiscard]] const APInt &get_unsigned_min_value() const {
    return known_one;
  }

  [[nodiscard]] APInt get_signed_max_value() const {
    // set all bits that can be one to one, but a msb that could be 0 is
    // cleared since that is the greater number
    APInt result = ~known_zero;
    if (msb_info() != KnownOne) {
      result.set_bit(width() - 1, false);
    }
    return result;
  }
  [[nodiscard]] APInt get_signed_min_value() const {
    APInt result = known_one;
    if (msb_info() == Unknown) {
      result.set_bit(width() - 1, true);
    }
    return result;
  }
};

static KnownBits computeForAddCarry(const KnownBits &LHS, const KnownBits &RHS,
                                    bool CarryZero, bool CarryOne) {
  const u32 W = LHS.width();
  const APInt PossibleSumZero = LHS.get_unsigned_max_value() +
                                RHS.get_unsigned_max_value() +
                                APInt(W, u64{!CarryZero});
  const APInt PossibleSumOne = LHS.get_unsigned_min_value() +
                               RHS.get_unsigned_min_value() +
                               APInt(W, u64{CarryOne});

  // Compute known bits of the carry.
  const APInt CarryKnownZero =
      ~(PossibleSumZero ^ LHS.known_zero ^ RHS.known_zero);
  const APInt CarryKnownOne = PossibleSumOne ^ LHS.known_one ^ RHS.known_one;

  // Compute set of known bits (where all three relevant bits are known).
  const APInt LHSKnownUnion = LHS.known_zero | LHS.known_one;
  const APInt RHSKnownUnion = RHS.known_zero | RHS.known_one;
  const APInt CarryKnownUnion = CarryKnownZero | CarryKnownOne;
  const APInt Known = LHSKnownUnion & RHSKnownUnion & CarryKnownUnion;

  // Compute known bits of the result.
  KnownBits KnownOut = KnownBits::unknown(W);
  KnownOut.known_zero = ~PossibleSumZero & Known;
  KnownOut.known_one = PossibleSumOne & Known;
  return KnownOut;
}

static KnownBits computeMul(const KnownBits &LHS, const KnownBits &RHS) {
  const u32 W = LHS.width();
  // second is a constant power of 2
  if (auto log2 = KnownBits::known_log2(RHS)) {
    auto res = KnownBits::unknown(W);
    res.known_one = LHS.known_one.shl(*log2);
    res.known_zero = LHS.known_zero.shl(*log2) | APInt::low_bits(W, *log2);
    return res;
  }

  const APInt maybe1l = ~LHS.known_zero;
  const APInt maybe1r = ~RHS.known_zero;
  auto res = KnownBits::unknown(W);
  if (maybe1l.is_zero() || maybe1r.is_zero()) {
    res.known_zero = APInt::all_ones(W);
    return res;
  }
  // the lowest bit that can be set of each side bounds the trailing zeros of
  // the product from below
  const u32 trailing = std::min(maybe1l.ctz() + maybe1r.ctz(), W);
  res.known_zero = APInt::low_bits(W, trailing);
  return res;
}

static KnownBits abs_known_bits(const KnownBits &input) {
  const u32 W = input.width();
  auto msb = input.msb_info();
  if (msb == KnownBits::BitInfo::KnownZero) {
    // if we know its positive
    return input;
  }
  if (msb == KnownBits::BitInfo::KnownOne) {
    // if we know its negative: -x = ~x + 1, ripple the carry through the known
    // low bits, once we hit an unknown bit all higher bits are unknown
    KnownBits res = KnownBits::unknown(W);
    bool carry = true;
    for (u32 i = 0; i < W; ++i) {
      const bool one = input.known_one.bit(i);
      const bool zero = input.known_zero.bit(i);
      if (!one && !zero) {
        break;
      }
      const bool inv = !one;
      const bool sum = inv != carry;
      if (sum) {
        res.known_one.set_bit(i);
      } else {
        res.known_zero.set_bit(i);
      }
      carry = inv && carry;
    }
    return res;
  }
  // idk sign
  // just try both and and them
  auto neg = input;
  neg.known_one.set_bit(W - 1, true);
  neg.known_zero.set_bit(W - 1, false);
  auto pos = input;
  pos.known_zero.set_bit(W - 1, true);
  pos.known_one.set_bit(W - 1, false);

  // Compute abs of negative version
  neg = abs_known_bits(neg);
  // Compute abs of positive version
  pos = abs_known_bits(pos);
  KnownBits res = KnownBits::unknown(W);
  res.known_one = pos.known_one & neg.known_one;
  res.known_zero = pos.known_zero & neg.known_zero;
  return res;
}

}  // namespace foptim::optim

inline fmt::appender fmt::formatter<foptim::optim::KnownBits>::format(
    foptim::optim::KnownBits const &v, format_context &ctx) const {
  auto app = ctx.out();
  for (foptim::u32 x = v.width(); x > 0; x--) {
    if (v.known_one.bit(x - 1)) {
      app = fmt::format_to(app, "1");
    } else if (v.known_zero.bit(x - 1)) {
      app = fmt::format_to(app, "0");
    } else {
      app = fmt::format_to(app, "?");
    }
  }
  return app;
}
