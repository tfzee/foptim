#pragma once
#include <fmt/core.h>

#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "utils/APInt.hpp"
#include "utils/bitset.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

// using utils::Int128;

class KnownBits;

static KnownBits computeForAddCarry(const KnownBits &LHS, const KnownBits &RHS,
                                    bool CarryZero, bool CarryOne);
static KnownBits computeMul(const KnownBits &LHS, const KnownBits &RHS);
static KnownBits abs_known_bits(const KnownBits &input);

class KnownBits final : public AttributeAnalysis {
 public:
  KnownBits() = default;
  ~KnownBits() override = default;

  u64 known_zero = 0;
  u64 known_one = 0;

  void materialize_impl(fir::Context &ctx,
                        const AttributerManager &m) override {
    (void)ctx;
    if (!associatedValue.is_instr()) {
      return;
    }
    auto instr = associatedValue.as_instr();
    if (instr->is(fir::BinaryInstrSubType::IntAdd)) {
      const auto *known_arg0_bits = m.get_analysis<KnownBits>(instr->args[0]);
      const auto *known_arg1_bits = m.get_analysis<KnownBits>(instr->args[1]);
      if ((known_arg0_bits == nullptr) || (known_arg1_bits == nullptr)) {
        return;
      }
      auto bitwidth =  // instr->get_type()->is_int()
                       //  ? instr->get_type()->as_int() :
          instr->get_type()->get_bitwidth();
      {
        auto biggest_val = (i128)known_arg0_bits->get_signed_max_value() +
                           (i128)known_arg1_bits->get_signed_max_value();
        auto smallest_val = (i128)known_arg0_bits->get_signed_min_value() +
                            (i128)known_arg1_bits->get_signed_min_value();
        // auto signed_max = ((i128)1 << bitwidth) - 1;
        // auto signed_min = ((i128)1 << bitwidth) - 1;
        auto signed_max = ((i128)1 << (bitwidth - 1)) - 1;
        auto signed_min = ((i128)-2) << (bitwidth - 1);
        if (biggest_val < signed_max && smallest_val > signed_min) {
          instr->NSW = true;
        }
      }
      {
        auto biggest_val = (i128)known_arg0_bits->get_unsigned_max_value() +
                           (i128)known_arg1_bits->get_unsigned_max_value();
        auto smallest_val = (i128)known_arg0_bits->get_unsigned_min_value() +
                            (i128)known_arg1_bits->get_unsigned_min_value();
        auto unsigned_max = ((i128)1 << bitwidth) - 1;
        auto unsigned_min = 0;
        if (biggest_val < unsigned_max && smallest_val > unsigned_min) {
          instr->NUW = true;
        }
      }
    }
  }

  enum BitInfo {
    Unknown = 0,
    KnownZero = 1,
    KnownOne = 2,
  };

  [[nodiscard]] BitInfo msb_info() const {
    auto size = associatedValue.get_type()->get_size() * 8;
    ASSERT(size <= 64);
    u64 mask = ((u64)0x1) << (size - 1);
    u64 is_one = known_one & mask;
    u64 is_zero = known_zero & mask;
    ASSERT(is_one == 0 || is_zero == 0);
    if (is_one != 0) {
      return BitInfo::KnownOne;
    }
    if (is_zero != 0) {
      return BitInfo::KnownZero;
    }
    return BitInfo::Unknown;
  }

  Result update_impl_instr(AttributerManager &m, Worklist &worklist) {
    u64 new_known_one = known_one;
    u64 new_known_zero = known_zero;
    auto instr = associatedValue.as_instr();
    if (instr->is(fir::InstrType::ZExt)) {
      auto old_bitwidth = instr->args[0].get_type()->get_size() * 8;
      const auto *known_arg_bits =
          m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
      u64 old_mask = ~0ULL;
      old_mask = old_mask >> (64 - old_bitwidth);
      u64 new_zero = ~0ULL;
      new_zero = new_zero << (old_bitwidth);

      new_known_one = known_arg_bits->known_one & old_mask;
      new_known_zero = known_arg_bits->known_zero & old_mask;
      new_known_zero |= new_zero;
    } else if (instr->is(fir::InstrType::ICmp) ||
               instr->is(fir::InstrType::FCmp)) {
      new_known_one = 0;
      new_known_zero = (~(u64)0) - 1;
    } else if (instr->is(fir::InstrType::Intrinsic)) {
      switch ((fir::IntrinsicSubType)instr->subtype) {
        case fir::IntrinsicSubType::Abs: {
          const auto *known_arg_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
          auto res = abs_known_bits(*known_arg_bits);
          new_known_one = res.known_one;
          new_known_zero = res.known_zero;
          break;
        }
        case fir::IntrinsicSubType::UMax: {
          const auto *known_arg0_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
          const auto *known_arg1_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[1], &worklist);
          if (known_arg0_bits->get_unsigned_min_value() >=
              known_arg1_bits->get_unsigned_max_value()) {
            new_known_one = known_arg0_bits->known_one;
            new_known_zero = known_arg0_bits->known_zero;
          } else if (known_arg1_bits->get_unsigned_min_value() >=
                     known_arg0_bits->get_unsigned_max_value()) {
            new_known_one = known_arg1_bits->known_one;
            new_known_zero = known_arg1_bits->known_zero;
          } else {
            new_known_one =
                known_arg0_bits->known_one & known_arg1_bits->known_one;
            new_known_zero =
                known_arg0_bits->known_zero & known_arg1_bits->known_zero;
          }
          break;
        }
        case fir::IntrinsicSubType::UMin: {
          const auto *known_arg0_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
          const auto *known_arg1_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[1], &worklist);
          if (known_arg0_bits->get_unsigned_max_value() <=
              known_arg1_bits->get_unsigned_min_value()) {
            new_known_one = known_arg0_bits->known_one;
            new_known_zero = known_arg0_bits->known_zero;
          } else if (known_arg1_bits->get_unsigned_max_value() <=
                     known_arg0_bits->get_unsigned_min_value()) {
            new_known_one = known_arg1_bits->known_one;
            new_known_zero = known_arg1_bits->known_zero;
          } else {
            new_known_one =
                known_arg0_bits->known_one & known_arg1_bits->known_one;
            new_known_zero =
                known_arg0_bits->known_zero & known_arg1_bits->known_zero;
          }
          break;
        }
        case fir::IntrinsicSubType::SMax: {
          const auto *known_arg0_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
          const auto *known_arg1_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[1], &worklist);
          if (known_arg0_bits->get_signed_min_value() >=
              known_arg1_bits->get_signed_max_value()) {
            new_known_one = known_arg0_bits->known_one;
            new_known_zero = known_arg0_bits->known_zero;
          } else if (known_arg1_bits->get_signed_min_value() >=
                     known_arg0_bits->get_signed_max_value()) {
            new_known_one = known_arg1_bits->known_one;
            new_known_zero = known_arg1_bits->known_zero;
          } else {
            new_known_one =
                known_arg0_bits->known_one & known_arg1_bits->known_one;
            new_known_zero =
                known_arg0_bits->known_zero & known_arg1_bits->known_zero;
          }
          break;
        }
        case fir::IntrinsicSubType::SMin: {
          const auto *known_arg0_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
          const auto *known_arg1_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[1], &worklist);

          // If arg0 is always <= arg1, result must be arg0
          if (known_arg0_bits->get_signed_max_value() <=
              known_arg1_bits->get_signed_min_value()) {
            new_known_one = known_arg0_bits->known_one;
            new_known_zero = known_arg0_bits->known_zero;
          }
          // If arg1 is always <= arg0, result must be arg1
          else if (known_arg1_bits->get_signed_max_value() <=
                   known_arg0_bits->get_signed_min_value()) {
            new_known_one = known_arg1_bits->known_one;
            new_known_zero = known_arg1_bits->known_zero;
          }
          // Otherwise, conservatively intersect known bits
          else {
            new_known_one =
                known_arg0_bits->known_one & known_arg1_bits->known_one;
            new_known_zero =
                known_arg0_bits->known_zero & known_arg1_bits->known_zero;
          }
          break;
        }
        case fir::IntrinsicSubType::CTLZ: {
          const auto *known_arg0_bits =
              m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
          if (known_arg0_bits->known_zero == 0) {
            // could loook at max value
            new_known_one = 0;
            new_known_zero = 0;
            break;
          }
          auto pre = __builtin_clzg(known_arg0_bits->known_zero);
          fmt::println(" {}\n{}", instr, *known_arg0_bits);
          fmt::println(" clz {} {}", pre, 128 - pre);
          std::abort();
          break;
        }
        case fir::IntrinsicSubType::FAbs:
        case fir::IntrinsicSubType::FMin:
        case fir::IntrinsicSubType::FMax:
          fmt::println("BITS KNOWN {}", *this);
          fmt::println("TODO: ATTRIB KNOWN BITS intrinsic OP {}",
                       associatedValue.as_instr());
          TODO("impl");
        case fir::IntrinsicSubType::INVALID:
        case fir::IntrinsicSubType::VA_start:
        case fir::IntrinsicSubType::VA_end:
          TODO("UNREACH");
      }
    } else if (instr->is(fir::InstrType::ITrunc)) {
      const auto *known_arg_bits =
          m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
      auto new_bitwidth = instr->get_type()->get_size() * 8;
      u64 new_mask = ~0ULL;
      new_mask = new_mask >> (64 - new_bitwidth);
      new_known_one = known_arg_bits->known_one & new_mask;
      new_known_zero = known_arg_bits->known_zero & new_mask;
    } else if (instr->is(fir::InstrType::AllocaInstr)) {
      // TODO: idk how to handle this best
      new_known_one = 0;
      new_known_zero = 0;
    } else if (instr->is(fir::InstrType::SelectInstr)) {
      const auto *known_arg1_bits =
          m.get_or_create_analysis<KnownBits>(instr->args[1], &worklist);
      const auto *known_arg2_bits =
          m.get_or_create_analysis<KnownBits>(instr->args[2], &worklist);
      new_known_one = known_arg1_bits->known_one & known_arg2_bits->known_one;
      new_known_zero =
          known_arg1_bits->known_zero & known_arg2_bits->known_zero;
    } else if (instr->is(fir::InstrType::SExt)) {
      auto old_bitwidth = instr->args[0].get_type()->get_size() * 8;
      const auto *known_arg_bits =
          m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
      u64 old_mask = ~0ULL;
      old_mask = old_mask >> (64 - old_bitwidth);

      new_known_one = known_arg_bits->known_one & old_mask;
      new_known_zero = known_arg_bits->known_zero & old_mask;
      auto old_msb = known_arg_bits->msb_info();
      if (old_msb == KnownOne) {
        u64 new_one = ~0ULL;
        new_one = new_one << (old_bitwidth);
        new_known_one |= new_one;
      } else if (old_msb == KnownZero) {
        u64 new_zero = ~0ULL;
        new_zero = new_zero << (old_bitwidth);
        new_known_zero |= new_zero;
      }
    } else if (instr->is(fir::InstrType::Conversion)) {
      switch ((fir::ConversionSubType)instr->subtype) {
        case fir::ConversionSubType::PtrToInt:
        case fir::ConversionSubType::IntToPtr: {
          const auto *a =
              m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
          new_known_one = a->known_one;
          new_known_zero = a->known_zero;
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
      const auto *a =
          m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
      switch ((fir::UnaryInstrSubType)instr->subtype) {
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
      const auto *a =
          m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
      const auto *b =
          m.get_or_create_analysis<KnownBits>(instr->args[1], &worklist);
      if (instr->subtype == (u32)fir::BinaryInstrSubType::IntAdd) {
        auto res = computeForAddCarry(*a, *b, true, false);
        new_known_one = res.known_one;
        new_known_zero = res.known_zero;
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::IntMul) {
        auto res = computeMul(*a, *b);
        new_known_one = res.known_one;
        new_known_zero = res.known_zero;
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::IntSub) {
        auto flippedB = KnownBits{};
        flippedB.known_one = b->known_zero;
        flippedB.known_zero = b->known_one;
        auto res = computeForAddCarry(*a, flippedB, false, true);
        new_known_one = res.known_one;
        new_known_zero = res.known_zero;
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::IntUDiv) {
        auto b_mask = (b->known_zero | b->known_one);
        auto b_const = b->known_one;
        // if its a power of 2
        if ((b_mask == ~0ULL && b_const > 0 &&
             ((b_const & (b_const - 1)) == 0))) {
          auto log2 = std::bit_width(b_const) - 1;
          new_known_one = a->known_one >> log2;
          new_known_zero = a->known_zero >> log2;
        }
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::IntSDiv) {
        auto b_mask = (b->known_zero | b->known_one);
        auto b_const = b->known_one;
        // if its a power of 2
        if ((b_mask == ~0ULL && b_const > 0 &&
             ((b_const & (b_const - 1)) == 0))) {
          auto log2 = std::bit_width(b_const) - 1;
          // verify that arithmetic shift is happening on signed shift right
          static_assert(12L >> 2 == 3L);
          static_assert(-12L >> 2 == -3L);
          new_known_one =
              std::bit_cast<u64>(std::bit_cast<i64>(a->known_one) >> log2);
          new_known_zero =
              std::bit_cast<u64>(std::bit_cast<i64>(a->known_zero) >> log2);
        }
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::Xor) {
        new_known_one = a->known_zero & b->known_one;
        new_known_zero =
            (a->known_zero & b->known_zero) | (a->known_one & b->known_one);
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::And) {
        new_known_one = a->known_one & b->known_one;
        new_known_zero = a->known_zero | b->known_zero;
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::Or) {
        new_known_one = a->known_one | b->known_one;
        new_known_zero = a->known_zero & b->known_zero;
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::Shr) {
        if ((b->known_one | b->known_zero) == ~(u64)1) {
          new_known_one = a->known_one >> b->known_one;
          new_known_zero = a->known_zero >> b->known_one;
        } else {
          // the top n bits are zero after the shift by atleast n
          auto min_shift = b->get_unsigned_min_value();
          new_known_one = 0;
          new_known_zero = ~(~(u64)0 >> min_shift);
        }
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::Shl) {
        if ((b->known_one | b->known_zero) == ~(u64)1) {
          new_known_one = a->known_one << b->known_one;
          new_known_zero = a->known_zero << b->known_one;
        } else {
          // the top n bits are zero after the shift by atleast n
          auto min_shift = b->get_unsigned_min_value();
          new_known_one = 0;
          new_known_zero = ~(~(u64)0 << min_shift);
        }
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::AShr) {
        if ((b->known_one | b->known_zero) == ~(u64)0) {
          // shift amount is known
          u64 shift = b->known_one;
          auto a_msb = a->msb_info();
          bool msb_known_one = a_msb == KnownOne;
          bool msb_known_zero = a_msb == KnownZero;

          new_known_one = a->known_one >> shift;
          new_known_zero = a->known_zero >> shift;

          // Sign extension for known MSB
          if (msb_known_one) {
            u64 sign_extension = ~(~(u64)0 >> shift);  // top 'shift' bits set
            new_known_one |= sign_extension;
          } else if (msb_known_zero) {
            u64 sign_extension =
                ~(~(u64)0 >> shift);  // top 'shift' bits cleared
            new_known_zero |= sign_extension;
          }
        } else {
          // shift amount is not fully known
          u64 min_shift = b->get_unsigned_min_value();

          // In this conservative case, we don't know shifted bits, but we can
          // propagate known sign bits into the upper bits
          new_known_one = 0;
          new_known_zero = 0;

          auto msb = a->msb_info();
          u64 sign_extension = ~(~(u64)0 >> min_shift);
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
    } else if (instr->is(fir::InstrType::LoadInstr)) {
    } else if (instr->is(fir::InstrType::CallInstr)) {
      // TODO: handle special builtin call instrs
    } else {
      fmt::println("BITS KNOWN {}", *this);
      fmt::println("TODO: ATTRIB KNOWN BITS {}", associatedValue.as_instr());
    }

    // mask the result
    auto bitwidth = associatedValue.get_type()->get_bitwidth();
    auto mask = ((u64)1 << bitwidth) - 1;
    new_known_one &= mask;
    new_known_zero &= mask;

    if (new_known_one != known_one || new_known_zero != known_zero) {
      if ((new_known_zero & new_known_one) != 0) {
        if (associatedValue.is_instr()) {
          fmt::println(
              "Failed known bits on {:c}\n it marked bits as both 1 and 0",
              associatedValue.as_instr());
        } else {
          fmt::println(
              "Failed known bits on {:c}\n it marked bits as both 1 and 0",
              associatedValue);
        }
        TODO("Failed");
      }
      known_zero = new_known_zero;
      known_one = new_known_one;
      return Result::Changed;
    }
    return Result::Fixed;
  }

  Result update_impl_constant() {
    u64 new_known_one = known_one;
    u64 new_known_zero = known_zero;

    auto constant = associatedValue.as_constant();
    switch (constant->ty) {
      case fir::ConstantType::PoisonValue:
        new_known_one = 0;
        new_known_zero = ~0ULL;
        break;
      case fir::ConstantType::IntValue: {
        auto v = std::bit_cast<u64>((i64)constant->as_int());
        new_known_one = v;
        new_known_zero = ~v;
      } break;
      case fir::ConstantType::FloatValue: {
        auto bitwidth = constant->get_type()->as_float();
        if (bitwidth == 64) {
          auto v = std::bit_cast<u64>(constant->as_f64());
          new_known_one = v;
          new_known_zero = ~v;
        } else if (bitwidth == 32) {
          auto v = std::bit_cast<u32>(constant->as_f32());
          new_known_one = v;
          new_known_zero = ~v;
        }
      } break;
      case fir::ConstantType::VectorValue:
      case fir::ConstantType::GlobalPtr:
      case fir::ConstantType::FuncPtr:
      case fir::ConstantType::NullPtr:
      case fir::ConstantType::ConstantStruct:
        break;
    }

    // mask the result
    // TODO: should known zero just be filled up the top bits??
    // nice english
    auto bitwidth = associatedValue.get_type()->get_bitwidth();
    auto mask = ((u64)1 << bitwidth) - 1;
    new_known_one &= mask;
    new_known_zero &= mask;

    if (new_known_one != known_one || new_known_zero != known_zero) {
      known_zero = new_known_zero;
      known_one = new_known_one;
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
      u64 new_known_one = 0;
      u64 new_known_zero = 0;
      auto bb_arg = associatedValue.as_bb_arg();
      auto parent = bb_arg->get_parent();
      auto bb_arg_id = bb_arg->get_parent()->get_arg_id(bb_arg);
      if (parent->get_n_uses() > 0) {
        new_known_one = ~0ULL;
        new_known_zero = ~0ULL;
        for (auto use : parent->uses) {
          ASSERT(use.type == fir::UseType::BB);
          auto input_value = use.user->bbs[use.argId].args[bb_arg_id];
          const auto *known_arg_bits =
              m.get_or_create_analysis<KnownBits>(input_value, &worklist);
          new_known_one = new_known_one & known_arg_bits->known_one;
          new_known_zero = new_known_zero & known_arg_bits->known_zero;
        }
        ASSERT((new_known_one & new_known_zero) == 0);
      }
      if (new_known_one != known_one || new_known_zero != known_zero) {
        known_zero = new_known_zero;
        known_one = new_known_one;
        return Result::Changed;
      }
    } else {
      fmt::println("TODO: ATTRIB KNOWN BITS {}", associatedValue);
    }
    return Result::Fixed;
  }

  [[nodiscard]] u64 get_unsigned_max_value() const { return ~known_zero; }
  [[nodiscard]] u64 get_unsigned_min_value() const { return known_one; }

  [[nodiscard]] i64 get_signed_max_value() const {
    auto msb = msb_info();
    // set all bits that can be one to one
    u64 result = ~known_zero;
    auto size = associatedValue.get_type()->get_bitwidth();
    if (msb == KnownZero || msb == Unknown) {
      // but mask out the msb to 0 if it could be 0 since that will be a
      // positive ie greater number
      ASSERT(size <= 64);
      u64 msb_mask = (u64)1 << (size - 1);
      result &= ~msb_mask;
    }
    u64 mask = (u64)(((u128)1 << size) - 1);
    i64 masked_result = (i64)(result & mask) << (64 - size) >> (64 - size);
    return masked_result;
  }
  [[nodiscard]] i64 get_signed_min_value() const {
    auto msb = msb_info();
    auto result = known_one;
    auto size = associatedValue.get_type()->get_bitwidth();
    if (msb == Unknown) {
      ASSERT(size <= 64);
      u64 msb_mask = (u128)0x1 << (size - 1);
      result |= msb_mask;
    }
    u64 mask = (u64)(((u128)1 << size) - 1);
    i64 masked_result = (i64)(result & mask) << (64 - size) >> (64 - size);
    return masked_result;
  }
};

static KnownBits computeForAddCarry(const KnownBits &LHS, const KnownBits &RHS,
                                    bool CarryZero, bool CarryOne) {
  u64 PossibleSumZero =
      LHS.get_unsigned_max_value() + RHS.get_unsigned_max_value() + !CarryZero;
  u64 PossibleSumOne =
      LHS.get_unsigned_min_value() + RHS.get_unsigned_min_value() + CarryOne;

  // Compute known bits of the carry.
  u64 CarryKnownZero = ~(PossibleSumZero ^ LHS.known_zero ^ RHS.known_zero);
  u64 CarryKnownOne = PossibleSumOne ^ LHS.known_one ^ RHS.known_one;

  // Compute set of known bits (where all three relevant bits are known).
  u64 LHSKnownUnion = LHS.known_zero | LHS.known_one;
  u64 RHSKnownUnion = RHS.known_zero | RHS.known_one;
  u64 CarryKnownUnion = std::move(CarryKnownZero) | CarryKnownOne;
  u64 Known = std::move(LHSKnownUnion) & RHSKnownUnion & CarryKnownUnion;

  // Compute known bits of the result.
  KnownBits KnownOut;
  KnownOut.known_zero = ~std::move(PossibleSumZero) & Known;
  KnownOut.known_one = std::move(PossibleSumOne) & Known;
  return KnownOut;
}
static KnownBits computeMul(const KnownBits &LHS, const KnownBits &RHS) {
  (void)LHS;

  auto b_mask = (RHS.known_zero | RHS.known_one);
  // if second is a constant + known power of 2
  if (b_mask == ~0ULL) {
    auto b_const = RHS.known_one;
    // if its a power of 2
    if (b_const > 0 && ((b_const & (b_const - 1)) == 0)) {
      auto log2 = std::bit_width(b_const) - 1;
      auto res = KnownBits{};
      res.known_one = LHS.known_one << log2;
      res.known_zero = LHS.known_zero << log2;
      res.known_zero |= ((u64)1 << log2) - 1;
      return res;
    }
  }

  auto maybe1l = ~LHS.known_zero;
  auto maybe1r = ~RHS.known_zero;
  if (maybe1l == 0 || maybe1r == 0) {
    auto res = KnownBits{};
    res.known_zero = ~(u64)0;
    return res;
  }
  auto res = KnownBits{};
  // IF we know the lowest set bit we know all lower bits in the result will
  // be
  // 0
  u32 llowest_bit_id = 0;
  u32 rlowest_bit_id = 0;
  while ((maybe1l & 1) == 0) {
    llowest_bit_id++;
    maybe1l >>= 1;
  }
  while ((maybe1r & 1) == 0) {
    rlowest_bit_id++;
    maybe1r >>= 1;
  }
  auto lowest_bit_id =
      llowest_bit_id > rlowest_bit_id ? llowest_bit_id : rlowest_bit_id;
  res.known_zero = ((u64)1 << lowest_bit_id) - 1;
  // TODO: impl properly
  return res;
}

static KnownBits abs_known_bits(const KnownBits &input) {
  auto msb = input.msb_info();
  if (msb == KnownBits::BitInfo::KnownZero) {
    // if we know its positive
    return input;
  }
  if (msb == KnownBits::BitInfo::KnownOne) {
    // if we know its negative
    uint64_t maybe_ones = input.known_one;
    uint64_t maybe_zeros = input.known_zero;

    uint64_t abs_known = 0;
    uint64_t abs_known_zero = 0;

    uint64_t carry = 1;
    for (int i = 0; i < 64; ++i) {
      uint64_t bit = 1ULL << i;

      // Check if bit is known
      bool one = (maybe_ones & bit) != 0;
      bool zero = (maybe_zeros & bit) != 0;

      if (one || zero) {
        bool inv = !one;         // ~x flips the known one
        bool sum = inv ^ carry;  // Compute the sum bit
        bool next_carry = inv & carry;

        abs_known |= bit;
        if (!sum) abs_known_zero |= bit;

        carry = next_carry;
      } else {
        // Once we hit an unknown bit, all higher bits become unknown
        break;
      }
    }

    KnownBits res;
    res.known_one = abs_known & ~abs_known_zero;
    res.known_zero = abs_known_zero;
    return res;
  }
  // idk sign
  // just try both and and them
  auto neg = input;
  neg.known_one |=
      1ULL << (input.associatedValue.get_type()->get_bitwidth() - 1);
  neg.known_zero &=
      ~(1ULL << (input.associatedValue.get_type()->get_bitwidth() - 1));
  auto pos = input;
  pos.known_zero |=
      1ULL << (input.associatedValue.get_type()->get_bitwidth() - 1);
  pos.known_one &=
      ~(1ULL << (input.associatedValue.get_type()->get_bitwidth() - 1));

  // Compute abs of negative version
  neg = abs_known_bits(neg);
  // Compute abs of positive version
  pos = abs_known_bits(pos);
  KnownBits res;
  res.known_one = pos.known_one & neg.known_one;
  res.known_zero = pos.known_zero & neg.known_zero;
  return res;
}

}  // namespace foptim::optim

fmt::appender fmt::formatter<foptim::optim::KnownBits>::format(
    foptim::optim::KnownBits const &v, format_context &ctx) const {
  auto app = ctx.out();
  auto o = v.known_one;
  auto z = v.known_zero;
  for (uint8_t x = 0; x < 64; x++) {
    auto o_t = o >> (63 - x);
    auto z_t = z >> (63 - x);
    if ((o_t & 1) == 1) {
      app = fmt::format_to(app, "1");
    } else if ((z_t & 1) == 1) {
      app = fmt::format_to(app, "0");
    } else {
      app = fmt::format_to(app, "?");
    }
  }
  return app;
}
