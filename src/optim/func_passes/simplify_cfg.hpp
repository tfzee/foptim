#pragma once
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

// + [ ] remove bb with no predecessor
// + [ ] merge bb with pred if thats only edge
// + [ ] eleminate bb args if only 1 pred
// + [ ] eliminate bb if only a single jump
// + [ ] convert if else into cmove
// + [ ]

class SimplifyCFG final : public FunctionPass {
public:
  void apply(fir::Context &, fir::Function &func) override {
    ZoneScopedN("SimplifyCFG");
    CFG cfg{func};
    // utils::Debug << func << "\n";

    for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
      auto &curr = cfg.bbrs[bb_id];
      if (curr.pred.empty() && bb_id != cfg.entry) {
        func.basic_blocks[bb_id]->remove_from_parent(true);
        cfg.update(func, false);
      }
      if (curr.succ.size() == 1 && cfg.bbrs[curr.succ[0]].pred.size() == 1) {
        auto succ_id = curr.succ[0];

        if (func.basic_blocks.at(bb_id)->n_args() != 0) {
          failure({"impl if previous has args", func.basic_blocks.at(bb_id)});
          continue;
        }
        if (func.basic_blocks.at(succ_id)->n_args() != 0) {
          failure({"impl if succ has args", func.basic_blocks.at(succ_id)});
          continue;
        }

        fir::Builder bb{func.basic_blocks[succ_id]};
        bb.at_start(func.basic_blocks[succ_id]);

        TMap<fir::ValueR, fir::ValueR> subs;

        for (size_t instr_id = 0;
             instr_id + 1 < func.basic_blocks[bb_id]->instructions.size();
             instr_id++) {
          auto &instr = func.basic_blocks[bb_id]->instructions[instr_id];
          auto new_instr = bb.insert_copy(instr);
          // utils::Debug << "SUBBING \n"
          //              << new_instr << "  subs: " << subs << "\n";
          new_instr.substitute(subs);
          subs.insert({fir::ValueR{instr}, fir::ValueR{new_instr}});
        }
        for(auto [from,to]: subs){
          fir::ValueR f = from;
          f.replace_all_uses(to);
        }
        
        func.basic_blocks[bb_id]->replace_all_uses(
            fir::ValueR{func.basic_blocks.at(succ_id)});
        func.basic_blocks[bb_id]->remove_from_parent(true);
        // utils::Debug << "\n)))))\n" << func << "\n====\n";
        cfg.update(func, false);
      }
      // if (func.basic_blocks[bb_id]->n_instrs() == 1 && curr.succ.size() == 1
      // &&
      //     func.basic_blocks.at(curr.succ[0])->n_args() == 0) {
      //   utils::Debug << func;
      //   TODO("impl delete if only jump");
      // }
    }
  }
};

} // namespace foptim::optim
