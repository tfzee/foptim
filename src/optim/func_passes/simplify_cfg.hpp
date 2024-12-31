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
// + [x] convert conditional branch with the same target into cmove
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
        // utils::Debug << 2 << "\n";
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
    // utils::Debug << 3 << "\n";
    return true;
  }

  // If we got bb args and multiple predecessors. If all incoming edges either
  // have the same value or are the bb arg itsself(a loop in which the bb arg
  // value doesnt change), then we can remove the bb arg and replace all uses
  // with the value itself
  if (curr.bb->n_args() != 0 && !is_entry) {
    auto n_args = curr.bb->n_args();
    // utils::Debug << "===============RUNNING ON BB:===============\n"
    //              << curr.bb << "\n";
    for (u32 ip1 = n_args; ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      fir::ValueR c_value{};
      bool is_c = true;
      // utils::Debug << "   BB_ARG: " << curr.bb->args[i] << "\n";

      TVec<fir::Use> uses{curr.bb->uses.begin(), curr.bb->uses.end()};

      for (auto use : uses) {
        ASSERT(use.type == fir::UseType::BB);
        auto pred_term = use.user;
        auto pred_term_bb_id = use.argId;
        auto incoming_arg = pred_term->bbs[pred_term_bb_id].args[i];
        // utils::Debug << "   INCMONIG: " << incoming_arg << " == " << c_value
        //              << "  " << !c_value.is_valid(false) << " "
        //              << (!c_value.is_valid(false) ||
        //              incoming_arg.eql(c_value))
        //              << "\n";
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
        // utils::Debug << "FOUND DEAD BB ARG\n";
        // utils::Debug << "  Value: " << c_value << "\n";
        // utils::Debug << "  Arg: " << curr.bb->args[i] << " in bb "
        //              << curr.bb.get_raw_ptr() << "\n";
        // TODO("FOUND ONE");
        for (auto use : uses) {
          // utils::Debug << "      USE: " << use.user << "\n";
          ASSERT(use.type == fir::UseType::BB);
          auto user = use.user;
          auto bb_id = user.get_bb_id(curr.bb);
          user.remove_bb_arg(bb_id, i);
          // utils::Debug << "      AFTER USE: " << use.user << "\n";
        }
        curr.bb->args[i]->replace_all_uses(c_value);
        curr.bb->args.erase(curr.bb->args.begin() + i);
        // utils::Debug << "      END: " << "\n";
        // utils::Debug << 32 << "\n";
        return true;
      }
    }
  }

  // if a block only has a single return we can move the return into all
  // previous blocks that have a single jump
  if (curr.bb->n_instrs() == 1 &&
      curr.bb->get_terminator()->is(fir::InstrType::ReturnInstr)) {
    const auto n_args = curr.bb->n_args();
    const auto return_instr = curr.bb->get_terminator();
    TMap<fir::ValueR, fir::ValueR> subs{};
    for (auto pred_id : curr.pred) {
      auto &pred = cfg.bbrs[pred_id];
      auto prev_term = pred.bb->get_terminator();
      if (pred.succ.size() != 1) {
        continue;
      }

      subs.clear();
      for (size_t bb_arg_id = 0; bb_arg_id < n_args; bb_arg_id++) {
        subs.insert({fir::ValueR{curr.bb->args[bb_arg_id]},
                     fir::ValueR{prev_term->bbs[0].args[bb_arg_id]}});
      }

      fir::Builder bb{pred.bb};
      bb.at_end(pred.bb);

      // bb.insert_copy(return_instr);
      auto new_return = bb.insert_copy(return_instr);
      new_return.substitute(subs);
      prev_term.remove_from_parent();
      // utils::Debug << pred.bb << "\n";
      // TODO("okak");
    }
    return true;
  }

  // if a block only contains a unconditional jump we can replace it
  // backwards(into pred) if there is no bb args or only 1 pred(secnd is handled
  // by other if)
  if (curr.succ.size() == 1 && cfg.bbrs[curr.succ[0]].bb->n_instrs() == 1 &&
      cfg.bbrs[curr.succ[0]].bb->n_args() == 0) {
    // utils::Debug << "PREV\n" << curr.bb->get_parent() << "\n";
    auto succ = cfg.bbrs[curr.succ[0]].bb;

    auto old_term = func.basic_blocks[bb_id]->get_terminator();

    fir::Builder bb{curr.bb};
    bb.at_end(curr.bb);

    bb.insert_copy(succ->get_terminator());

    old_term.remove_from_parent();
    succ->remove_from_parent(true);
    // utils::Debug << "AFTER\n"
    //              << curr.bb->get_parent() << "\n===================";
    return true;
  }

  // if a block only contains a unconditional
  //  jump we can replace it forwards if were not the entry block
  //  TODO: this implentation uses succ this wont work for the entry block
  if (curr.bb->n_instrs() == 1 &&
      curr.bb->get_terminator()->is(fir::InstrType::BranchInstr) && !is_entry) {
    ASSERT(curr.succ.size() == 1);
    auto succ = cfg.bbrs[curr.succ[0]].bb;
    // if no bb args involved just replace
    if (curr.bb->n_args() == 0 && succ->n_args() == 0) {
      curr.bb->replace_all_uses(fir::ValueR(succ));
      func.basic_blocks[bb_id]->remove_from_parent(true);
      // utils::Debug << 4 << "\n";
      return true;
    }
    // if this block doesnt have bb_args we might be able to replace all
    // incoming edges with the outgoig jump + args
    if (curr.bb->n_args() == 0) {
      const auto &terminator_args = curr.bb->get_terminator()->bbs[0].args;
      TVec<fir::Use> uses(curr.bb->get_uses().begin(),
                          curr.bb->get_uses().end());
      for (const auto &use : uses) {
        ASSERT(use.type == fir::UseType::BB);
        auto bb_id = use.argId;
        auto user = use.user;
        ASSERT(user->bbs[bb_id].args.size() == 0);
        user.replace_bb(bb_id, succ, false);

        ASSERT(user->bbs[bb_id].args.size() == 0);
        for (auto old_arg : terminator_args) {
          user.add_bb_arg(bb_id, old_arg);
        }
        ASSERT(user->bbs[bb_id].args.size() == terminator_args.size());
        utils::Debug << user << "\n";
      }
      ASSERT(curr.bb->get_n_uses() == 0);
      func.basic_blocks[bb_id]->remove_from_parent(true);
      // utils::Debug << 5 << "\n";
      return true;
    }
  }

  // if 1 to 1 relation between blocks we can merge them
  // TODO: this should in theory even work with multiple incmoing and then use a
  // heuristic so it can do it for any short enough block
  if (curr.succ.size() == 1 && cfg.bbrs[curr.succ[0]].pred.size() == 1 &&
      !is_entry) {
    auto succ_id = curr.succ[0];

    // bool first_has_args = func.basic_blocks.at(bb_id)->n_args() != 0;
    bool secon_has_args = func.basic_blocks.at(succ_id)->n_args() != 0;
    auto old_first_term = func.basic_blocks[bb_id]->get_terminator();

    if (secon_has_args) {
      auto succ = func.basic_blocks[succ_id];
      // for (auto arg: succ->args) {
      //   arg->replace_all_uses(term->bbs[0].args[i]);
      // }
      for (u32 i = 0; i < succ->n_args(); i++) {
        succ->args[i]->replace_all_uses(old_first_term->bbs[0].args[i]);
      }
      succ->clear_args();
    }
    // if (first_has_args) {
    //   for (auto arg : func.basic_blocks[bb_id]->args) {
    //     auto new_arg = func.basic_blocks[succ_id].add_arg(ctx->copy(arg));
    //     arg->replace_all_uses(fir::ValueR{new_arg});
    //   }
    // }

    fir::Builder bb{curr.bb};
    bb.at_end(curr.bb);

    TMap<fir::ValueR, fir::ValueR> subs;

    for (size_t instr_id = 0;
         instr_id < func.basic_blocks[succ_id]->instructions.size();
         instr_id++) {
      auto &instr = func.basic_blocks[succ_id]->instructions[instr_id];
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

    // func.basic_blocks[bb_id]->replace_all_uses(
    //     fir::ValueR{func.basic_blocks.at(succ_id)});
    old_first_term.remove_from_parent();
    func.basic_blocks[succ_id]->remove_from_parent(true);
    // utils::Debug << 6 << "\n";
    return true;
  }

  // If we got a conditionalbrach with both taking the same target
  // then we can cmove the bbargs and then do a simple branch
  auto terminator = curr.bb->get_terminator();
  if (curr.succ.size() == 2 && terminator->bbs[0].bb == terminator->bbs[1].bb) {
    // auto *ctx = curr.bb->get_parent()->ctx;

    auto &args1 = terminator->bbs[0].args;
    auto &args2 = terminator->bbs[1].args;
    auto condition = terminator->get_arg(0);
    auto target = terminator->bbs[0].bb;

    // TODO: prob needs some heursitics if its worth it
    fir::Builder bb{curr.bb};
    bb.at_penultimate(curr.bb);
    TVec<fir::ValueR> new_inputs;
    for (size_t i = 0; i < args1.size(); i++) {
      new_inputs.push_back(bb.build_select(ctx->copy(args1[i].get_type()),
                                           condition, args1[i], args2[i]));
    }
    auto new_branch = bb.build_branch(target);
    for (auto &input : new_inputs) {
      new_branch.add_bb_arg(0, input);
    }
    terminator.remove_from_parent();
    return true;
  }

  // utils::Debug << "DEAD" << "\n";
  return false;
}

} // namespace foptim::optim
