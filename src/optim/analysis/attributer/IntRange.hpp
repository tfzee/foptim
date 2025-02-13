#pragma once
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/APInt.hpp"
#include <limits>

namespace foptim::optim {

using utils::Int128;

class IntRange final : public AttributeAnalysis {
public:
  IntegerLattice<Int128, 0, std::numeric_limits<Int128>::min()> min;
  IntegerLattice<Int128, 0, std::numeric_limits<Int128>::max()> max;
  IntRange() = default;
  ~IntRange() override = default;
  void materialize_impl(fir::Context &ctx) override {
    fir::ConstantValueR max_val =
        fir::ConstantValueR{fir::ConstantValueR::invalid()};
    fir::ConstantValueR min_val =
        fir::ConstantValueR{fir::ConstantValueR::invalid()};
    max_val = ctx->get_constant_value(std::bit_cast<i64>(max.value.low),
                                      ctx->get_int_type(64));
    min_val = ctx->get_constant_value(std::bit_cast<i64>(min.value.low),
                                      ctx->get_int_type(64));

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
      // auto value = associatedValue.as_constant()->as_int();
      TODO("idk how to do these maybe the constant should store their type/should be bigint APInt");
      return Result::Fixed;
    }
    if (associatedValue.is_bb_arg()) {
      utils::Debug << "TODO\n";
      return Result::Fixed;
    }
    if (!associatedValue.is_instr()) {
      return Result::Fixed;
    }

    auto instr = associatedValue.as_instr();
    if (instr->is(fir::InstrType::BinaryInstr) &&
        (fir::BinaryInstrSubType)instr->subtype ==
            fir::BinaryInstrSubType::IntAdd) {
      auto a1 = m.get_or_create_analysis<IntRange>(instr->args[0], &worklist);
      auto a2 = m.get_or_create_analysis<IntRange>(instr->args[1], &worklist);
      if (a1->min.isWorst() || a2->min.isWorst()) {
        max.value = max.getWorst();
        return Result::Changed;
      } else {
        TODO("impl");
      }

      if (a1->max.isWorst() || a2->max.isWorst()) {
        max.value = max.getWorst();
        return Result::Changed;
      } else {
        TODO("impl");
      }
    }

    return Result::Fixed;
  }
};

} // namespace foptim::optim
