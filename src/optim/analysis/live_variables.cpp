#include "live_variables.hpp"
#include "ir/basic_block_ref.hpp"
#include "utils/arena.hpp"

namespace foptim::optim {

fir::Instr get_last_use_in_bb(fir::ValueR value, fir::BasicBlock target_bb) {
  size_t sub_pos = 0;
  fir::Instr final_use_instr{fir::Instr::invalid()};
  bool found = false;
  for (const fir::Use &use : *value.get_uses()) {
    // only if the use is in this bb
    if (use.user->get_parent() == target_bb) {
      found = true;
      size_t location =
          std::ranges::find(target_bb->instructions.begin(),
                            target_bb->instructions.end(), use.user) -
          target_bb->instructions.begin();
      // add 1 since the end of the Location is offset by 1
      if (location >= sub_pos) {
        final_use_instr = use.user;
        sub_pos = location;
      }
    }
  }

  if (!found) {
    utils::Debug << value;
    utils::Debug << "Didnt find " << value << " in bb " << target_bb << "\n";
    ASSERT(found);
  }
  ASSERT(final_use_instr.is_valid());
  return final_use_instr;
}

void LiveVariables::dump() {
  for (auto [var, ranges] : live_variables) {
    utils::Debug << "Var " << var << "\n";
    for (auto range : ranges) {
      utils::Debug << "  " << range << "\n";
    }
  }
}

void LiveVariables::update(fir::Function &func, CFG &cfg) {
  ZoneScopedN("LIVEVAR UPDATE");

  IRVec<utils::BitSet<>> upwExp;
  IRVec<utils::BitSet<>> defs;

  const auto all_values = setup_values(func);
  const size_t n_values = all_values.size();

  upwExp.resize(func.n_bbs(), utils::BitSet{n_values, false});
  defs.resize(func.n_bbs(), utils::BitSet{n_values, false});
  std::deque<u32, utils::TempAlloc<u32>> worklist{};

  // setup constant values for data flow equation
  auto &bbs = func.get_bbs();
  for (size_t bb_id = 0; bb_id < bbs.size(); bb_id++) {
    worklist.push_front(bb_id);
    auto &bb = bbs[bb_id];
    for (u32 arg_id = 0; arg_id < bb->n_args(); arg_id++) {
      defs[bb_id][all_values.at(fir::ValueR(bb->args[arg_id]))].set(true);
    }
    for (auto instr : bb->instructions) {
      if (all_values.contains(fir::ValueR(instr))) {
        ASSERT(instr->get_parent() == bb);
        defs[bb_id][all_values.at(fir::ValueR(instr))].set(true);
      }

      for (auto arg : instr->args) {
        if (arg.is_constant()) {
          continue;
        }
        if (!all_values.contains(arg)) {
          utils::Debug << "Didnt find value for " << arg << "\n";
          ASSERT(false);
        }
        const size_t arg_id = all_values.at(arg);
        if (!defs[bb_id][arg_id]) {
          upwExp[bb_id][arg_id].set(true);
        }
      }
      for (auto &bb_arg : instr->bbs) {
        for (auto arg : bb_arg.args) {
          if (arg.is_constant()) {
            continue;
          }
          if (!all_values.contains(arg)) {
            utils::Debug << "Didnt find value for " << arg << "\n";
            ASSERT(false);
          }
          const size_t arg_id = all_values.at(arg);
          if (!defs[bb_id][arg_id]) {
            upwExp[bb_id][arg_id].set(true);
          }
        }
      }
    }
  }

  // utils::Debug << "defs\n";
  // for (auto def : defs) {
  //   utils::Debug << def << "\n";
  // }
  // utils::Debug << "upwExp\n";
  // for (auto upwExp : upwExp) {
  //   utils::Debug << upwExp << "\n";
  // }

  // data flow
  IRVec<utils::BitSet<>> liveIn;
  IRVec<utils::BitSet<>> liveOut;
  liveIn.resize(func.n_bbs(), utils::BitSet{n_values, false});
  liveOut.resize(func.n_bbs(), utils::BitSet{n_values, true});
  utils::BitSet new_liveOut{n_values, false};
  utils::BitSet new_liveIn{n_values, false};

  while (!worklist.empty()) {
    u32 curr_id = worklist.front();
    worklist.pop_front();
    new_liveOut.reset(false);

    for (auto succ : cfg.bbrs[curr_id].succ) {
      new_liveOut += liveIn[succ];
    }
    new_liveIn.assign(new_liveOut).mul_not(defs[curr_id]).add(upwExp[curr_id]);
    // utils::Debug << "Updating " << curr_id << " " << new_liveOut << "  " <<
    // new_liveIn
    //              << "\n";
    // auto test = upwExp[curr_id] + (new_liveOut - defs[curr_id]);
    // assert(test == liveIn[curr_id]);

    if (new_liveOut != liveOut[curr_id]) {
      liveOut[curr_id].assign(new_liveOut);
      worklist.push_back(curr_id);
    }
    if (new_liveIn != liveIn[curr_id]) {
      liveIn[curr_id].assign(new_liveIn);
      for (auto pred : cfg.bbrs[curr_id].pred) {
        worklist.push_back(pred);
      }
    }
  }

  // utils::Debug << "LIVEIN\n";
  // for (auto live_in : liveIn) {
  //   utils::Debug << live_in << "\n";
  // }
  // utils::Debug << "LIVEOUT\n";
  // for (auto live_out : liveOut) {
  //   utils::Debug << live_out << "\n";
  // }

  // utils::Debug << "ACT LIVE\n";

  utils::BitSet bb_live{n_values, false};

  for (u32 bb_id = 0; bb_id < bbs.size(); bb_id++) {
    const auto &bb_liveOut = liveOut[bb_id];
    const auto &bb_liveIn = liveIn[bb_id];
    const auto &bb_defs = defs[bb_id];
    bb_live.assign(bb_liveOut).add(bb_defs).add(bb_liveIn);
    // assert(bb_live == bb_liveOut + bb_defs + bb_liveIn);

    // utils::Debug << "For BB:" << bb_id << "\n";
    // utils::Debug << "--Live: " << bb_live << "\n";

    for (size_t value_id = 0; value_id < n_values; value_id++) {
      bool val_liveIn = bb_liveIn[value_id];
      bool val_liveOut = bb_liveOut[value_id];
      bool val_defined = bb_defs[value_id];
      bool val_live = val_liveIn || val_liveOut || val_defined;

      auto value_ref =
          std::ranges::find_if(all_values, [value_id](const auto &v) {
            return v.second == value_id;
          });
      // utils::Debug << "  For: " << value_ref->first << "\n";
      // is it live at all
      if (!val_live) {
        continue;
      }
      // utils::Debug << "   LiveIn: " << val_liveIn << "\n";
      // utils::Debug << "   LiveOut: " << val_liveOut << "\n";
      // utils::Debug << "   LiveDef: " << val_defined << "\n";

      auto &live_ranges = live_variables[value_ref->first];
      ASSERT(value_ref != all_values.end());
      ASSERT(value_ref->second == value_id);

      if (val_defined && value_ref->first.is_instr()) {
        if (value_ref->first.as_instr()->get_parent() != bbs[bb_id]) {
          utils::Debug << value_ref->first << "\n";
        }
        ASSERT(value_ref->first.as_instr()->get_parent() == bbs[bb_id]);
      }

      // TODO: clean this up into 2x 2 distinc cases of entry or value inside of
      // bb
      if (value_ref->first.get_n_uses() == 0 && value_ref->first.is_instr()) {
        // unused resutl of critical instruction can be discarded and needs no
        // lifetime?
        continue;
      }

      auto bb = func.get_bbs()[bb_id];
      u32 first_use_in_bb = 0;
      u32 last_use_in_bb = bb->instructions.size();
      if (!val_liveIn && val_defined && value_ref->first.is_instr()) {
        first_use_in_bb =
            fir::IRLocation::get_instr_indx(value_ref->first.as_instr()) + 1;
      }
      if (!val_liveOut && value_ref->first.get_n_uses() != 0) {
        last_use_in_bb = fir::IRLocation::get_instr_indx(
            get_last_use_in_bb(value_ref->first, func.get_bbs()[bb_id]));
      }

      live_ranges.emplace_back(bb_id, first_use_in_bb, last_use_in_bb);
    }
  }
  // dump();
  // std::abort();
}

} // namespace foptim::optim
