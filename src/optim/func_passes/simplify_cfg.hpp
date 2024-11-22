#pragma once
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/function_pass.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

// + [x] remove bb with no predecessor
// + [x] merge bb with pred if thats only edge
// + [x] eleminate bb args if only 1 pred
// + [x] eliminate bb if only a single jump
// + [ ] convert if else into cmove
// + [ ]

inline bool simplify_cfg(CFG &cfg, fir::Function &func, size_t bb_id) {
  auto &curr = cfg.bbrs[bb_id];
  // utils::Debug << bb_id << " ";
  // if not jumped to just delete
  if (curr.pred.empty() && bb_id != cfg.entry) {
    func.basic_blocks[bb_id]->remove_from_parent(true);
    // utils::Debug << 1 << "\n";
    return true;
  }

  // if only 1 pred we can replace all the bb args with just the values of
  // the pred
  if (curr.pred.size() == 1 && curr.bb->n_args() != 0) {
    auto n_args = curr.bb->n_args();
    auto pred_term = cfg.bbrs[curr.pred[0]].bb->get_terminator();
    auto pred_term_bb_id = pred_term.get_bb_id(curr.bb);
    for (u32 i = 0; i < n_args; i++) {
      curr.bb->args[i].replace_all_uses(
          pred_term->bbs[pred_term_bb_id].args[i]);
    }
    curr.bb->clear_args();
    pred_term.clear_bb_args(pred_term_bb_id);
    // utils::Debug << 2 << "\n";
    return true;
  }

  // if a block only contains a unconditional jump we can replace it
  if (curr.bb->n_instrs() == 1 &&
      curr.bb->get_terminator()->is(fir::InstrType::BranchInstr)) {
    auto succ = cfg.bbrs[curr.succ[0]].bb;
    if (curr.bb->n_args() != 0 || succ->n_args() != 0) {
      return false;
    }
    curr.bb->replace_all_uses(fir::ValueR(succ));
    func.basic_blocks[bb_id]->remove_from_parent(true);
    // utils::Debug << 3 << "\n";
    return true;
  }

  // if 1 to 1 relation between blocks we can merge them
  // TODO this implentation uses succ this wont work for the entry block
  if (curr.succ.size() == 1 && cfg.bbrs[curr.succ[0]].pred.size() == 1 &&
      bb_id != cfg.entry) {
    auto succ_id = curr.succ[0];

    bool first_has_args = func.basic_blocks.at(bb_id)->n_args() != 0;
    bool secon_has_args = func.basic_blocks.at(succ_id)->n_args() != 0;

    if (secon_has_args) {
      auto term = func.basic_blocks[bb_id]->get_terminator();
      auto succ = func.basic_blocks[succ_id];
      for (u32 i = 0; i < succ->n_args(); i++) {
        fir::ValueR{succ, i}.replace_all_uses(term->bbs[0].args[i]);
      }
      succ->clear_args();
    }
    if (first_has_args) {
      for (auto arg : func.basic_blocks[bb_id]->args) {
        auto new_arg = func.basic_blocks[succ_id].add_arg(arg.type);
        arg.replace_all_uses(new_arg);
      }
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
    for (auto [from, to] : subs) {
      fir::ValueR f = from;
      f.replace_all_uses(to);
    }

    func.basic_blocks[bb_id]->replace_all_uses(
        fir::ValueR{func.basic_blocks.at(succ_id)});
    func.basic_blocks[bb_id]->remove_from_parent(true);
    // utils::Debug << 4 << "\n";
    return true;
  }
  return false;
}

class SimplifyCFG final : public FunctionPass {
public:
  void apply(fir::Context &, fir::Function &func) override {
    ZoneScopedN("SimplifyCFG");
    CFG cfg{func};
    // utils::Debug << func << "\n";

    auto iter = 0;
    for (size_t bb_id = 1; bb_id <= cfg.bbrs.size(); bb_id++) {
      iter++;
      if (simplify_cfg(cfg, func, bb_id - 1)) {
        cfg.update(func, false);
        bb_id = 0;
      }
      if (iter > 1000) {
        failure({"Didnt converge fixme\n", func.basic_blocks[bb_id]});
        return;
      }
    }
  }
};

} // namespace foptim::optim
