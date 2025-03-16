#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include "utils/set.hpp"
#include <algorithm>

namespace foptim::optim {

static bool reachable_from_entry(CFG &cfg, size_t bb_id) {
  if (cfg.entry == bb_id) {
    return true;
  }

  TSet<size_t> seen{};
  std::deque<size_t, utils::TempAlloc<size_t>> worklist;
  worklist.push_back(cfg.entry);

  while (!worklist.empty()) {
    auto curr_id = worklist.back();
    worklist.pop_back();
    if (seen.contains(curr_id)) {
      continue;
    }
    seen.insert(curr_id);
    for (auto succ : cfg.bbrs[curr_id].succ) {
      if (bb_id == succ) {
        return true;
      }
      if (seen.contains(succ)) {
        continue;
      }
      worklist.push_back(succ);
    }
  }
  return false;
}

class DCE final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("DCE");
    CFG rev_cfg{func, true};
    Dominators rev_dom{rev_cfg};
    (void)ctx;
    // if (func.linkage == fir::Function::Linkage::Internal &&
    //     func.get_n_uses() == 0) {
    //   ASSERT(ctx->delete_function(func.name.c_str()));
    //   return;
    // }

    TSet<fir::Instr> marked{};
    std::deque<fir::Instr, utils::TempAlloc<fir::Instr>> worklist;
    // first mark all critical operations
    // and add them to the worklist
    for (auto &bb : func.get_bbs()) {
      for (auto &instr : bb->get_instrs()) {
        bool isCritical = instr->is_critical();

        if (isCritical) {
          worklist.push_back(instr);
          marked.insert(instr);
        }
      }
    }

    // then workoff the worklist
    while (!worklist.empty()) {
      auto curr_i = worklist.front();
      worklist.pop_front();
      // since worklist contains only crticial instructions we can mark all
      // arguments as cirtical and add them aswell to the worklist
      for (auto arg : curr_i->get_args()) {
        if (arg.is_instr()) {
          auto arg_instr = arg.as_instr();
          if (std::get<1>(marked.insert(arg_instr))) {
            worklist.push_back(arg_instr);
          }
        } else if (arg.is_bb_arg()) {
          auto term = arg.as_bb_arg()->get_parent()->get_terminator();
          if (std::get<1>(marked.insert(term))) {
            worklist.push_back(term);
          }
        }
      }
      // same with bb arguments
      for (const auto &bb_with_args : curr_i->get_bb_args()) {
        auto term = bb_with_args.bb->get_terminator();
        if (std::get<1>(marked.insert(term))) {
          worklist.push_back(term);
        }
        // and the args for them
        for (auto bb_arg : bb_with_args.args) {
          if (bb_arg.is_instr()) {
            auto arg_instr = bb_arg.as_instr();
            if (std::get<1>(marked.insert(arg_instr))) {
              worklist.push_back(arg_instr);
            }
          } else if (bb_arg.is_bb_arg()) {
            auto term = bb_arg.as_bb_arg()->get_parent()->get_terminator();
            if (std::get<1>(marked.insert(term))) {
              worklist.push_back(term);
            }
          }
        }
      }

      // furthermore all blocks in the reverse domintor frontier become also
      // critical since they decide if this instruction gets executed to mark
      // them as critical we mark the last branch isntruction as such
      const size_t bb_id =
          std::find(func.basic_blocks.begin(), func.basic_blocks.end(),
                    curr_i->get_parent()) -
          func.basic_blocks.begin();
      for (auto bb_id : rev_dom.dom_bbs[bb_id].frontier) {
        auto term = func.basic_blocks[bb_id]->get_terminator();
        if (std::get<1>(marked.insert(term))) {
          worklist.push_back(term);
        }
      }
    }

    // then all critical operations should be marked
    // dump(func, marked);

    // now we iterate over them and sweep
    // this means deleting instructions that are not terminators
    // while not marked conditional branches can be replaced with a jump to the
    // closest non dead post dominator

    for (auto &bb : func.get_bbs()) {
      const auto n_instrs = bb->get_instrs().size();
      for (size_t instr_id_p1 = n_instrs; instr_id_p1 > 0; instr_id_p1--) {
        auto instr = bb->get_instrs()[instr_id_p1 - 1];
        if (marked.contains(instr) || instr->is(fir::InstrType::BranchInstr)) {
          continue;
        }

        if (instr->is(fir::InstrType::CondBranchInstr)) {
          IMPL("handle dce cond branch simplification");
        }

        bb->remove_instr(instr_id_p1 - 1, true);
      }
    }

    CFG cfg{func};
    TVec<size_t> dead_blocks;
    for (size_t bb_id = 0; bb_id < func.basic_blocks.size(); bb_id++) {
      if (func.basic_blocks[bb_id] != func.get_entry() &&
          !reachable_from_entry(cfg, bb_id)) {
        dead_blocks.push_back(bb_id);
      }
    }
    // reverse so we run high ones first
    std::reverse(dead_blocks.begin(), dead_blocks.end());
    for (auto dead_bb_id : dead_blocks) {
      func.basic_blocks[dead_bb_id]->remove_from_parent(true, true, true);
    }
  }

  void dump(fir::Function &func, TSet<fir::Instr> &marked) {
    (void)func;
    (void)marked;
    TODO("REIMPL");
    // for (auto &bb : func.get_bbs()) {
    //   print << "BB;\n";
    //   for (auto &instr : bb->get_instrs()) {
    //     print << (marked.contains(instr) ? " " : "X") << "    " << instr
    //                  << "\n";
    //   }
    // }
  }
};
} // namespace foptim::optim
