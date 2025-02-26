
#pragma once
#include "../function_pass.hpp"
#include "ir/builder.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "utils/logging.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

class LICM final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    CFG cfg{func};
    Dominators dom{cfg};
    LoopInfoAnalysis linfo{dom};

    for (auto &info : linfo.info) {
      apply(ctx, func, cfg, info);
    }
  }

  void apply(fir::Context & /*unused*/, fir::Function &func, CFG &cfg,
             LoopInfo &info) {
    (void)func;

    if (cfg.bbrs[info.head].pred.size() != info.tails.size() + 1) {
      failure({"TODO: Cant apply licm without a preheader\n",
               cfg.bbrs[info.head].bb});
      return;
    }

    fir::BasicBlock pre_header{fir::BasicBlock{fir::BasicBlock::invalid()}};
    for (auto i : cfg.bbrs[info.head].pred) {
      if (std::find(info.body_nodes.begin(), info.body_nodes.end(), i) ==
          info.body_nodes.end()) {
        ASSERT(!pre_header.is_valid());
        pre_header = cfg.bbrs[i].bb;
        // TODO: break
      }
    }
    if (!pre_header.is_valid()) {
      info.dump();
      // print << cfg.bbrs[info.head].pred.size() << "\n";
      // print << cfg.bbrs[info.head].bb->get_parent() << "\n";
      // print << cfg.bbrs[info.head].pred << "\n";
      ASSERT(false);
      failure({"TODO: Cant apply licm without a preheader\n",
               cfg.bbrs[info.head].bb});
      return;
    }

    std::deque<fir::Instr, utils::TempAlloc<fir::Instr>> worklist;
    TVec<fir::ValueR> invariant;

    for (auto bb : info.body_nodes) {
      for (auto instr : cfg.bbrs[bb].bb->instructions) {
        if (instr->pot_modifies_mem() || instr->has_pot_sideeffects() ||
            instr->is_critical() || instr->is(fir::InstrType::LoadInstr)) {
          continue;
        }
        worklist.push_back(instr);
      }
    }

    while (!worklist.empty()) {
      auto instr = worklist.front();
      worklist.pop_front();
      if (instr->pot_modifies_mem() || instr->has_pot_sideeffects() ||
          instr->is_critical()) {
        continue;
      }

      bool all_args_are_invariant = true;
      for (auto arg : instr->args) {
        fir::BasicBlock arg_bb{fir::BasicBlock{fir::BasicBlock::invalid()}};
        if (arg.is_instr()) {
          arg_bb = arg.as_instr()->get_parent();
        } else if (arg.is_bb_arg()) {
          arg_bb = arg.as_bb_arg()->get_parent();
        } else if (arg.is_constant()) {
          continue;
        } else {
          UNREACH();
        }
        auto arg_bb_id = cfg.get_bb_id(arg_bb);

        if (std::find(invariant.begin(), invariant.end(), arg) ==
                invariant.end() &&
            std::find(info.body_nodes.begin(), info.body_nodes.end(),
                      arg_bb_id) != info.body_nodes.end()) {
          all_args_are_invariant = false;
          break;
        }
      }
      if (all_args_are_invariant) {
        invariant.emplace_back(instr);
        // TODO: push uses into worklist but only if they are still in teh loop
      }
    }
    if(invariant.empty()){
      return;
    }

    
    fir::Builder bb{pre_header};
    bb.at_penultimate(pre_header);
    for (auto inv : invariant) {
      auto old = inv.as_instr();
      auto copy = bb.insert_copy(old);
      old->replace_all_uses(fir::ValueR{copy});
    }

    for (auto inv : invariant) {
      inv.as_instr().remove_from_parent();
    }
    // TODO("");
  }
};

} // namespace foptim::optim
