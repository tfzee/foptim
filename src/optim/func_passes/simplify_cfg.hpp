#pragma once
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/function_pass.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

// + [x] remove bb with no predecessor
// + [x] merge bb with pred if thats only edge
// + [x] eliminate bb if only a single jump
// + [x] eleminate bb args if only 1 pred
// + [x] eliminate bb arg if only one unique incoming value
// + [x] eliminate bb arg if no uses
// + [ ] convert if else into cmove
// + [ ]

class SimplifyCFG final : public FunctionPass {
public:
  bool simplify_cfg(CFG &cfg, fir::Function &func, size_t bb_id);
  void apply(fir::Context & /*unused*/, fir::Function &func) override {
    ZoneScopedN("SimplifyCFG");
    // utils::Debug << func << "\n";
    CFG cfg{func};

    auto iter = 0;
    for (size_t bb_id = 1; bb_id <= cfg.bbrs.size(); bb_id++) {
      iter++;
      if (simplify_cfg(cfg, func, bb_id - 1)) {
        cfg.update(func, false);
        bb_id = 0;
      }
      ASSERT(func.verify(utils::Debug));
      if (iter > 1000) {
        failure({"Didnt converge fixme\n", func.basic_blocks[bb_id]});
        return;
      }
    }
    // utils::Debug << func << "\n";
    // utils::Debug << "END\n" << "\n";
  }
};

inline bool SimplifyCFG::simplify_cfg(CFG &cfg, fir::Function &func,
                                      size_t bb_id) {
  auto &curr = cfg.bbrs[bb_id];
  auto *ctx = curr.bb->get_parent()->ctx;
  bool is_entry = bb_id == cfg.entry;
  // utils::Debug << bb_id << " ";
  // if not jumped to just delete
  if (curr.pred.empty() && !is_entry) {
    func.basic_blocks[bb_id]->remove_from_parent(true);
    // utils::Debug << 1 << "\n";
    return true;
  }

  // if we got a bb arg that got no use remove it
  if (curr.bb->n_args() != 0 && !is_entry) {
    auto n_args = curr.bb->n_args();
      // for (u32 i = 0; i < n_args; i++) {
    for (u32 ip1 = n_args; ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      if (curr.bb->args[i]->get_n_uses() == 0) {
        // remove from all uses where we jump into this bb
        for (auto use : curr.bb->get_uses()) {
          ASSERT(use.type == fir::UseType::BB);
          use.user.remove_bb_arg(use.argId, i);
        }

        // drop this arg we cant have any uses
        curr.bb->args.erase(curr.bb->args.begin() + i);
        return true;
      }
    }
  }

  // if only 1 pred we can replace all the bb args with just the values of
  // the pred
  if (curr.pred.size() == 1 && curr.bb->n_args() != 0) {
    auto n_args = curr.bb->n_args();
    auto pred_term = cfg.bbrs[curr.pred[0]].bb->get_terminator();
    auto pred_term_bb_id = pred_term.get_bb_id(curr.bb);
    for (u32 i = 0; i < n_args; i++) {
      curr.bb->args[i]->replace_all_uses(
          pred_term->bbs[pred_term_bb_id].args[i]);
    }
    curr.bb->clear_args();
    pred_term.clear_bb_args(pred_term_bb_id);
    // utils::Debug << 2 << "\n";
    return true;
  }

  // If we got bb args and multiple predecessors. If all incoming edges either
  // have the same value or are the bb arg itsself(a loop in which the bb arg
  // value doesnt change), then we can remove the bb arg and replace all uses
  // with the value itself
  if (curr.bb->n_args() != 0 && !is_entry) {
    auto n_args = curr.bb->n_args();
    for (u32 ip1 = n_args; ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      fir::ValueR c_value{};
      bool is_c = true;
      // utils::Debug << "   BB_ARG: " << curr.bb->args[i] << "\n";
      for (auto pred : curr.pred) {
        auto pred_term = cfg.bbrs[pred].bb->get_terminator();
        auto pred_term_bb_id = pred_term.get_bb_id(curr.bb);
        auto incoming_arg = pred_term->bbs[pred_term_bb_id].args[i];
        // utils::Debug << "   INCMONIG: " << incoming_arg << "\n";
        if (!c_value.is_valid(false) || incoming_arg.eql(c_value)) {
          c_value = incoming_arg;
        } else if (incoming_arg.is_bb_arg() &&
                   incoming_arg.as_bb_arg() == curr.bb->args[i]) {
        } else {
          is_c = false;
          break;
        }
      }
      if (is_c) {
        utils::Debug << "FOUND DEAD BB ARG\n";
        utils::Debug << "  Value: " << c_value << "\n";
        utils::Debug << "  Arg: " << curr.bb->args[i] << " in bb "
                     << curr.bb.get_raw_ptr() << "\n";
        // utils::Debug << "TODO IMPL IT\n";
        // TODO("FOUND ONE");
        for (auto use : curr.bb->uses) {
          utils::Debug << "      USE: " << use.user << "\n";
          ASSERT(use.type == fir::UseType::BB);
          auto user = use.user;
          auto bb_id = user.get_bb_id(curr.bb);
          user.remove_bb_arg(bb_id, i);
          utils::Debug << "      AFTER USE: " << use.user << "\n";
        }

        curr.bb->args[i]->replace_all_uses(c_value);
        curr.bb->args.erase(curr.bb->args.begin() + i);
        // utils::Debug << "      END: " << "\n";
        // return false;
      }
    }
  }

  // if a block only contains a unconditional jump we can replace it
  // TODO: this implentation uses succ this wont work for the entry block
  if (curr.bb->n_instrs() == 1 &&
      curr.bb->get_terminator()->is(fir::InstrType::BranchInstr) && !is_entry) {
    ASSERT(curr.succ.size() == 1);
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
  // TODO: this implentation uses succ this wont work for the entry block
  if (curr.succ.size() == 1 && cfg.bbrs[curr.succ[0]].pred.size() == 1 &&
      !is_entry) {
    auto succ_id = curr.succ[0];

    bool first_has_args = func.basic_blocks.at(bb_id)->n_args() != 0;
    bool secon_has_args = func.basic_blocks.at(succ_id)->n_args() != 0;

    if (secon_has_args) {
      auto term = func.basic_blocks[bb_id]->get_terminator();
      auto succ = func.basic_blocks[succ_id];
      // for (auto arg: succ->args) {
      //   arg->replace_all_uses(term->bbs[0].args[i]);
      // }
      for (u32 i = 0; i < succ->n_args(); i++) {
        succ->args[i]->replace_all_uses(term->bbs[0].args[i]);
      }
      succ->clear_args();
    }
    if (first_has_args) {
      for (auto arg : func.basic_blocks[bb_id]->args) {
        auto new_arg = func.basic_blocks[succ_id].add_arg(ctx->copy(arg));
        arg->replace_all_uses(fir::ValueR{new_arg});
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

} // namespace foptim::optim
