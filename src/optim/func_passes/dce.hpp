#pragma once
#include <algorithm>

#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/arena.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

static void reachable_from_entry(CFG &cfg, TSet<size_t> &seen) {
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
      if (seen.contains(succ)) {
        continue;
      }
      worklist.push_back(succ);
    }
  }
}

class DCE final : public FunctionPass {
 public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("DCE", COLOR_OPTIMF);
    {
      CFG rev_cfg{func, true};
      Dominators rev_dom{rev_cfg};
      // if (func.linkage == fir::Function::Linkage::Internal &&
      //     func.get_n_uses() == 0) {
      //   // fmt::println("{}", func.name.c_str());
      //   ctx->delete_function(func.name.c_str());
      //   return;
      // }

      TSet<fir::Instr> marked{};
      TVec<fir::Instr> worklist;
      for (auto &bb : func.get_bbs()) {
        for (auto &instr : bb->get_instrs()) {
          bool isCritical = instr->is_critical();

          if (isCritical) {
            worklist.push_back(instr);
            marked.insert(instr);
          }
        }
      }
      // dump(func, marked);

      // then workoff the worklist
      while (!worklist.empty()) {
        auto curr_i = worklist.back();
        worklist.pop_back();
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
        const size_t bb_id = rev_cfg.get_bb_id(curr_i->get_parent());
        for (auto bb_id : rev_dom.dom_bbs.at(bb_id).frontier) {
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
      // while not marked conditional branches can be replaced with a jump to
      // the closest non dead post dominator

      for (auto &bb : func.get_bbs()) {
        const auto n_instrs = bb->get_instrs().size();
        for (size_t instr_id_p1 = n_instrs; instr_id_p1 > 0; instr_id_p1--) {
          auto instr = bb->get_instrs()[instr_id_p1 - 1];
          // killing alloca that only get written to but never read or never
          // used
          if (instr->is(fir::InstrType::AllocaInstr)) {
            bool kill = true;
            for (auto &use : instr->uses) {
              // TODO: could handle stuff like add and stuff here aswell to be
              // more precise read lower note for issue tho
              if (use.user->is(fir::InstrType::StoreInstr) && use.argId == 0) {
                continue;
              }
              kill = false;
              break;
            }
            if (kill) {
              // NOTE: since we know all uses are store instructions we can just
              // delete them aswell
              (void)ctx;
              auto uses = instr->uses;
              for (auto &use : uses) {
                use.user.destroy();
              }
              instr.destroy();
              continue;
            }
          }
          if (marked.contains(instr) ||
              instr->is(fir::InstrType::BranchInstr)) {
            continue;
          }

          if (instr->is(fir::InstrType::CondBranchInstr)) {
            IMPL("handle dce cond branch simplification");
          }

          instr->replace_all_uses(
              fir::ValueR{ctx->get_poisson_value(instr.get_type())});
          bb->remove_instr(instr_id_p1 - 1, true);
        }
      }
      utils::TempAlloc<void>::reset();
    }

    {
      CFG cfg{func};
      TSet<size_t> seen;
      reachable_from_entry(cfg, seen);
      // reverse so we remove high ones first
      for (size_t bb_idp1 = func.basic_blocks.size(); bb_idp1 > 0; bb_idp1--) {
        if (func.basic_blocks[bb_idp1 - 1] != func.get_entry() &&
            !seen.contains(bb_idp1 - 1)) {
          // for (auto instr : func.basic_blocks[bb_idp1 - 1]->instructions) {
          //   instr->replace_all_uses(
          //       fir::ValueR{ctx->get_poisson_value(instr.get_type())});
          // }
          func.basic_blocks[bb_idp1 - 1]->remove_from_parent(true, true, true);
        }
      }
    }
  }

  void dump(fir::Function &func, TSet<fir::Instr> &marked) {
    for (auto &bb : func.get_bbs()) {
      fmt::println("BB;\n");
      for (auto &instr : bb->get_instrs()) {
        fmt::println("{}    {:cd}", marked.contains(instr) ? "X" : " ", instr);
      }
    }
  }
};
}  // namespace foptim::optim
