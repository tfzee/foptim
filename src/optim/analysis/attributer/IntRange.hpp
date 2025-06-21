#pragma once
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "utils/APInt.hpp"
#include <limits>

namespace foptim::optim {

// using utils::Int128;

class IntRange final : public AttributeAnalysis {
public:
  IntegerLattice<i128, 0, std::numeric_limits<i128>::min()> min;
  IntegerLattice<i128, 0, std::numeric_limits<i128>::max()> max;
  IntRange() = default;
  ~IntRange() override = default;
  void materialize_impl(fir::Context &ctx) override {
    fir::ConstantValueR max_val =
        fir::ConstantValueR{fir::ConstantValueR::invalid()};
    fir::ConstantValueR min_val =
        fir::ConstantValueR{fir::ConstantValueR::invalid()};
    max_val = ctx->get_constant_value((i64)(max.value), ctx->get_int_type(64));
    min_val = ctx->get_constant_value((i64)(min.value), ctx->get_int_type(64));

    if (associatedValue.is_instr()) {
      if (!max.isWorst()) {
        associatedValue.as_instr()->add_attrib("IntRange::max", max_val);
      }
      if (!min.isWorst()) {
        associatedValue.as_instr()->add_attrib("IntRange::min", min_val);
      }
    } else if (associatedValue.is_bb_arg()) {
      if (!max.isWorst()) {
        associatedValue.as_bb_arg()->add_attrib("IntRange::max", max_val);
      }
      if (!min.isWorst()) {
        associatedValue.as_bb_arg()->add_attrib("IntRange::min", min_val);
      }
    }
  }

  Result update_impl(AttributerManager &m, Worklist &worklist) override {
    if (associatedValue.is_constant()) {
      if (min.isWorst() || max.isWorst()) {
        auto value = associatedValue.as_constant()->as_int();
        min.value = value;
        max.value = value;
        return Result::Changed;
      }
      return Result::Fixed;
    }
    if (associatedValue.is_bb_arg()) {
      // TODO: impl
      return Result::Fixed;
    }
    if (!associatedValue.is_instr()) {
      return Result::Fixed;
    }

    auto instr = associatedValue.as_instr();
    bool changed = false;
    if (instr->is(fir::InstrType::BinaryInstr)) {
      auto *a1 = m.get_or_create_analysis<IntRange>(instr->args[0], &worklist);
      auto *a2 = m.get_or_create_analysis<IntRange>(instr->args[1], &worklist);
      bool worst_min = a1->min.isWorst() || a2->min.isWorst();
      bool worst_max = a1->max.isWorst() || a2->max.isWorst();

      switch ((fir::BinaryInstrSubType)instr->subtype) {
      case fir::BinaryInstrSubType::IntAdd: {
        auto new_min_val = worst_min ? min.getWorst() : a1->min + a2->min;
        if (new_min_val != min.value) {
          min.value = new_min_val;
          changed = true;
        }
        auto new_max_val = worst_max ? max.getWorst() : a1->max + a2->max;
        if (new_max_val != max.value) {
          max.value = new_max_val;
          changed = true;
        }
        break;
      }
      case fir::BinaryInstrSubType::IntMul: {
        auto new_min_val = worst_min ? min.getWorst() : a1->min * a2->min;
        if (new_min_val != min.value) {
          min.value = new_min_val;
          changed = true;
        }
        auto new_max_val = worst_max ? max.getWorst() : a1->max * a2->max;
        if (new_max_val != max.value) {
          max.value = new_max_val;
          changed = true;
        }
        break;
      }
      default:
        break;
      }
    }

    return changed ? Result::Changed : Result::Fixed;
  }
};

} // namespace foptim::optim
