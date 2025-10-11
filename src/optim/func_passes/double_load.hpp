#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/function_pass.hpp"
#include "utils/set.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

namespace {
void intersection(TSet<fir::Instr> &curr, const TSet<fir::Instr> &remove_data) {
  for (auto it = curr.begin(); it != curr.end();) {
    if (!remove_data.contains(*it)) {
      it = curr.erase(it);  // erase returns the next iterator
    } else {
      ++it;
    }
  }
}

void cut(TSet<fir::Instr> &curr, AliasAnalyis &aa, bool all_overwritten,
         const TVec<fir::ValueR> &remove_data) {
  if (all_overwritten) {
    curr.clear();
    return;
  }
  for (auto it = curr.begin(); it != curr.end();) {
    bool modified = false;
    for (auto remove_d : remove_data) {
      // fmt::println("{} {} {} {}", remove_d, it->get_raw_ptr()->args[0],
      //              aa.analyze(remove_d).heap,
      //              aa.analyze(it->get_raw_ptr()->args[0]).heap);
      if (aa.alias(remove_d, it->get_raw_ptr()->args[0]) !=
          AliasAnalyis::AAResult::NoAlias) {
        it = curr.erase(it);
        modified = true;
        break;
      }
    }
    if (!modified) {
      ++it;
    }
  }
}
}  // namespace

class DoubleLoadElim final : public FunctionPass {
 public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("DoubleLoadElim", COLOR_OPTIMF);
    (void)ctx;
    TVec<TSet<fir::Instr>> active_loads;
    TVec<TVec<fir::Instr>> downard_exp_loads;
    TVec<std::pair<bool, TVec<fir::ValueR>>> over_writen_ptrs;

    active_loads.resize(func.n_bbs());
    downard_exp_loads.resize(func.n_bbs());
    over_writen_ptrs.resize(func.n_bbs());

    CFG cfg{func};
    AliasAnalyis aa;
    TVec<fir::BasicBlock> worklist;

    for (auto bb : func.basic_blocks) {
      worklist.push_back(bb);
      auto bb_id = cfg.get_bb_id(bb);
      over_writen_ptrs[bb_id].first = false;
      for (auto instr : bb->instructions) {
        if (instr->is(fir::InstrType::LoadInstr)) {
          downard_exp_loads[bb_id].push_back(instr);
        }
        if (instr->is(fir::InstrType::StoreInstr)) {
          // TODO: do a aliasing check
          downard_exp_loads[bb_id].clear();
          over_writen_ptrs[bb_id].second.push_back(instr->args[0]);
        } else if (instr->pot_modifies_mem()) {
          downard_exp_loads[bb_id].clear();
          over_writen_ptrs[bb_id].first = true;
        }
      }
      active_loads[bb_id].insert(downard_exp_loads[bb_id].cbegin(),
                                 downard_exp_loads[bb_id].cend());
    }

    TVec<fir::Instr> helper;

    while (!worklist.empty()) {
      fir::BasicBlock curr = worklist.back();
      worklist.pop_back();
      auto curr_id = cfg.get_bb_id(curr);
      if (cfg.bbrs[curr_id].pred.empty()) {
        continue;
      }

      auto &res = active_loads[curr_id];
      helper.clear();
      for (auto b : res) {
        helper.push_back(b);
      }
      res.clear();
      res.insert(active_loads[cfg.bbrs[curr_id].pred[0]].cbegin(),
                 active_loads[cfg.bbrs[curr_id].pred[0]].cend());
      for (auto prev : cfg.bbrs[curr_id].pred) {
        auto &prev_data = active_loads[prev];
        intersection(res, prev_data);
      }
      cut(res, aa, over_writen_ptrs[curr_id].first,
          over_writen_ptrs[curr_id].second);
      active_loads[curr_id].insert(downard_exp_loads[curr_id].cbegin(),
                                   downard_exp_loads[curr_id].cend());
      bool modified = false;
      if (helper.size() != res.size()) {
        modified = true;
      } else {
        for (auto x : helper) {
          if (!res.contains(x)) {
            modified = true;
            break;
          }
        }
      }
      if (modified) {
        for (auto succ : cfg.bbrs[curr_id].succ) {
          worklist.push_back(cfg.bbrs[succ].bb);
        }
      }
    }

    // Dominators dom{cfg};
    for (size_t b = 0; b < cfg.bbrs.size(); b++) {
      auto &bb_i = cfg.bbrs[b];
      for (auto instr : bb_i.bb->instructions) {
        if (instr->is(fir::InstrType::LoadInstr)) {
          for (auto l : active_loads[b]) {
            if (l->parent == bb_i.bb) {
              continue;
            }
            if (l->args[0] != instr->args[0]) {
              continue;
            }
            // if (!dom.dom_bbs[b].dominators[cfg.get_bb_id(l->parent)]) {
            //   continue;
            // }
            instr->replace_all_uses(fir::ValueR{l});
            utils::StatCollector::get().addi(1, "DoubleLoadElim");
            // instr.destroy();
            break;
          }
        }
      }
    }
    // for (size_t b = 0; b < cfg.bbrs.size(); b++) {
    //   fmt::println("==BBBB===");
    //   fmt::println("  ======DOWNLOAD=====");
    //   for (auto l : downard_exp_loads[b]) {
    //     fmt::println("   {:cd}", l);
    //   }
    //   fmt::println("  ======STR=====");
    //   fmt::println("  ======OVERWRITES EVERYTHING: {}",
    //                over_writen_ptrs[b].first);
    //   for (auto b : over_writen_ptrs[b].second) {
    //     fmt::println("   {:cd}", b);
    //   }
    //   fmt::println("  ======RES=====");
    //   for (auto l : active_loads[b]) {
    //     fmt::println("   {:cd}", l);
    //   }
    // }
    // TODO("okak");
  }
};
}  // namespace foptim::optim
