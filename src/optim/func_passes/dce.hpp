#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <unordered_set>

namespace foptim::optim {

class DCE final : public FunctionPass {
public:
  void apply(fir::Context &, fir::Function &func) override {
    ZoneScopedN("DCE");
    CFG rev_cfg{func, true};
    Dominators rev_dom{rev_cfg};

    std::unordered_set<fir::Instr> marked{};
    std::deque<fir::Instr> worklist;
    // first mark all critical operations
    // and add them to the worklist
    for (auto &bb : func.get_bbs()) {
      for (auto &instr : bb->get_instrs()) {
        bool isCritical = true;
        switch (instr->get_instr_type()) {
        case fir::InstrType::BinaryInstr:
        case fir::InstrType::SExt:
        case fir::InstrType::LoadInstr:
        case fir::InstrType::AllocaInstr:
        case fir::InstrType::CondBranchInstr:
        case fir::InstrType::ICmp:
        case fir::InstrType::BranchInstr:
          isCritical = false;
          break;
        case fir::InstrType::ReturnInstr:
        case fir::InstrType::DirectCallInstr:
        case fir::InstrType::StoreInstr:
          isCritical = true;
          break;
        }

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
          auto term = arg.as_bb_arg().bb->get_terminator();
          if (std::get<1>(marked.insert(term))) {
            worklist.push_back(term);
          }
        }
      }
      // same with bb arguments
      for (auto bb_with_args : curr_i->get_bb_args()) {
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
            auto term = bb_arg.as_bb_arg().bb->get_terminator();
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
          // TODO: handle it
          std::abort();
          continue;
        }

        bb->remove_instr(instr_id_p1 - 1);
      }
    }

    
  }

  void dump(fir::Function &func, std::unordered_set<fir::Instr> marked) {
    for (auto &bb : func.get_bbs()) {
      utils::Debug << "BB;\n";
      for (auto &instr : bb->get_instrs()) {
        utils::Debug << (marked.contains(instr) ? " " : "X") << "    " << instr
                     << "\n";
      }
    }
  }
};
} // namespace foptim::optim
