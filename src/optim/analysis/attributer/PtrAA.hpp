#pragma once
#include "attributer.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"

namespace foptim::optim {

class PtrAA final : public AttributeAnalysis {
public:
  IntegerLattice<bool, true, false> known_non_null;

  ~PtrAA() override = default;
  void materialize_impl() override {
    utils::Debug << "MATERIALIZED!! on " << associatedValue << "\n";
    if (associatedValue.is_instr()) {
      associatedValue.as_instr()->add_attrib("nonull", "");
    } else if (associatedValue.is_bb_arg()) {
      associatedValue.as_bb_arg()->add_attrib("nonull", "");
    }
  }

  Result update_impl(AttributerManager & /*m*/, Worklist &) override {
    if (known_non_null.isBest()) {
      return Result::Fixed;
    }

    fir::BasicBlock value_bb{fir::BasicBlock::invalid()};
    if (associatedValue.is_instr()) {
      value_bb = associatedValue.as_instr()->get_parent();
    } else if (associatedValue.is_bb_arg()) {
      value_bb = associatedValue.as_bb_arg()->get_parent();
    } else {
      return Result::Fixed;
    }
    CFG cfg{*value_bb->get_parent().func};
    Dominators dom(cfg);

    auto value_bb_id = cfg.get_bb_id(value_bb);

    for (auto &use : *associatedValue.get_uses()) {
      if (use.user->is(fir::InstrType::LoadInstr) ||
          use.user->is(fir::InstrType::StoreInstr)) {
        auto target_bb = use.user->get_parent();
        auto target_bb_id = cfg.get_bb_id(target_bb);
        if (dom.dom_bbs[target_bb_id].dominators[value_bb_id]) {
          known_non_null = true;
          return Result::Changed;
        }
      }
    }

    return Result::Fixed;
  }
};

} // namespace foptim::optim
