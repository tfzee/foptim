#pragma once
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/APInt.hpp"
#include <limits>

namespace foptim::optim {

// using utils::Int128;

class KnownBits;

static KnownBits computeForAddCarry(const KnownBits &LHS, const KnownBits &RHS,
                                    bool CarryZero, bool CarryOne);

class KnownBits final : public AttributeAnalysis {
public:
  KnownBits() = default;
  ~KnownBits() override = default;

  u64 known_zero = 0;
  u64 known_one = 0;

  void materialize_impl(fir::Context &ctx) override { (void)ctx; }

  enum BitInfo {
    Unknown = 0,
    KnownZero = 1,
    KnownOne = 2,
  };

  [[nodiscard]] BitInfo msb_info() const {
    auto size = associatedValue.get_type()->get_size() * 8;
    ASSERT(size <= 64);
    u64 mask = 0x1 << (size - 1);
    auto is_one = known_one & mask;
    auto is_zero = known_zero & mask;
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
    } else if (instr->is(fir::InstrType::ITrunc)) {
      const auto *known_arg_bits =
          m.get_or_create_analysis<KnownBits>(instr->args[0], &worklist);
      auto new_bitwidth = instr->get_type()->get_size() * 8;
      u64 new_mask = ~0ULL;
      new_mask = new_mask >> (64 - new_bitwidth);
      new_known_one = known_arg_bits->known_one & new_mask;
      new_known_zero = known_arg_bits->known_zero & new_mask;
    } else if (instr->is(fir::InstrType::SelectInstr)) {
      const auto *known_arg1_bits =
          m.get_or_create_analysis<KnownBits>(instr->args[1], &worklist);
      const auto *known_arg2_bits =
          m.get_or_create_analysis<KnownBits>(instr->args[2], &worklist);
      new_known_one = known_arg1_bits->known_one & known_arg2_bits->known_one;
      new_known_zero = known_arg1_bits->known_one & known_arg2_bits->known_one;
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
      case fir::ConversionSubType::INVALID:
      case fir::ConversionSubType::FPEXT:
      case fir::ConversionSubType::FPTRUNC:
      case fir::ConversionSubType::FPTOUI:
      case fir::ConversionSubType::FPTOSI:
      case fir::ConversionSubType::UITOFP:
      case fir::ConversionSubType::SITOFP:
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
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::IntSub) {
        auto flippedB = KnownBits{};
        flippedB.known_one = b->known_zero;
        flippedB.known_zero = b->known_one;
        auto res = computeForAddCarry(*a, flippedB, false, true);
        new_known_one = res.known_one;
        new_known_zero = res.known_zero;
      } else if (instr->subtype == (u32)fir::BinaryInstrSubType::IntMul) {
        auto b_mask = (b->known_zero | b->known_one);
        auto b_const = b->known_one;
        // if its a power of 2
        if ((b_mask == ~0ULL && b_const > 0 &&
             ((b_const & (b_const - 1)) == 0))) {
          auto log2 = std::bit_width(b_const) - 1;
          new_known_one = a->known_one << log2;
          new_known_zero = a->known_zero << log2;
        }
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
      } else {
        fmt::println("TODO: ATTRIB KNOWN BITS BIINARY OP {}",
                     associatedValue.as_instr());
      }
    } else if (instr->is(fir::InstrType::LoadInstr)) {
    } else if (instr->is(fir::InstrType::CallInstr)) {
      // TODO: handle special builtin call instrs
    } else {
      fmt::println("TODO: ATTRIB KNOWN BITS {}", associatedValue.as_instr());
    }
    if (new_known_one != known_one || new_known_zero != known_zero) {
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
      u64 new_known_one = known_one;
      u64 new_known_zero = known_zero;
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
    if (msb == KnownZero || msb == Unknown) {
      // but mask out the msb to 0 if it could be 0 since that will be a
      // positive ie greater number
      auto size = associatedValue.get_type()->get_size() * 8;
      ASSERT(size <= 64);
      u64 msb_mask = 0x1 << (size - 1);
      result &= ~msb_mask;
    }
    return std::bit_cast<i64>(result);
  }
  [[nodiscard]] i64 get_signed_min_value() const {
    auto msb = msb_info();
    auto result = known_one;
    if (msb == Unknown) {
      auto size = associatedValue.get_type()->get_size() * 8;
      ASSERT(size <= 64);
      u64 msb_mask = 0x1 << (size - 1);
      result |= msb_mask;
    }
    return std::bit_cast<i64>(result);
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

} // namespace foptim::optim
