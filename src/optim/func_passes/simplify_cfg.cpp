#include "simplify_cfg.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "utils/logging.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

bool SimplifyCFG::remove_dead_bb(CFG & /*cfg*/, CFG::Node &curr,
                                 fir::Function &func, size_t bb_id,
                                 bool is_entry) {
  if (curr.pred.empty() && !is_entry) {
    func.basic_blocks[bb_id]->remove_from_parent(true);
    return true;
  }
  return false;
}

// check for true uses (so a  bb arg  that is actually used in an instruction)
//  we might have a loop of multiple bbs that just forward a bb so it will
//  have uses but all are just forwarding to the next bb
bool has_true_use(fir::BBArgument v) {
  TVec<fir::Use> worklist;
  TSet<fir::Use> seen;
  for (auto use : v->get_uses()) {
    worklist.push_back(use);
  }
  while (!worklist.empty()) {
    auto item = worklist.back();
    worklist.pop_back();

    if (seen.contains(item)) {
      continue;
    }
    seen.insert(item);

    if (item.type == fir::UseType::NormalArg) {
      return true;
    } else if (item.type == fir::UseType::BB) {
      UNREACH();
    } else if (item.type == fir::UseType::BBArg) {
      auto new_item = item.user->bbs[item.argId].bb->args[item.bbArgId];
      for (auto use : new_item->get_uses()) {
        worklist.push_back(use);
      }
    }
  }
  return false;
}

bool SimplifyCFG::remove_dead_bb_arg(CFG & /*cfg*/, CFG::Node &curr,
                                     fir::Function &func, size_t /*bb_id*/,
                                     bool is_entry) {
  if (curr.bb->n_args() != 0 && !is_entry) {
    auto n_args = curr.bb->n_args();
    for (u32 ip1 = n_args; ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      auto &value = curr.bb->args[i];
      if (value->get_n_uses() == 0) {
        // remove from all uses where we jump into this bb
        for (auto use : curr.bb->get_uses()) {
          ASSERT(use.type == fir::UseType::BB);
          use.user.remove_bb_arg(use.argId, i);
        }

        // drop this arg we cant have any uses
        curr.bb->args.erase(curr.bb->args.begin() + i);
        return true;
      } else if (!has_true_use(value)) {
        for (auto use : curr.bb->get_uses()) {
          ASSERT(use.type == fir::UseType::BB);
          use.user.remove_bb_arg(use.argId, i);
        }

        value->replace_all_uses(
            fir::ValueR{func.ctx->get_poisson_value(value->get_type())});

        curr.bb->args.erase(curr.bb->args.begin() + i);
        return true;
      }
    }
  }
  return false;
}

bool SimplifyCFG::dup_bb_to_args(CFG &cfg, CFG::Node &bb1, fir::Function &func,
                                 size_t bb_id, bool is_entry) {
  auto ctx = func.ctx;
  (void)bb_id;
  (void)is_entry;
  bool found = false;
  struct DiffConst {
    fir::Instr instr;
    u32 arg_id;
    fir::ValueR old_val;
    fir::Instr instr2;
    u32 arg_id2;
  };
  TVec<DiffConst> difference_values;
  fir::BasicBlock res_bb1 = fir::BasicBlock(fir::BasicBlock::invalid());
  fir::BasicBlock res_bb2 = fir::BasicBlock(fir::BasicBlock::invalid());

  for (auto bb2 : cfg.bbrs) {
    if (bb1.bb == bb2.bb ||
        bb1.bb->instructions.size() != bb2.bb->instructions.size()) {
      continue;
    }

    found = true;
    for (size_t i = 0; i < bb1.bb->instructions.size(); i++) {
      auto i1 = bb1.bb->instructions[i];
      auto i2 = bb2.bb->instructions[i];
      // TODO: shouldnt this be any constant differences between 2 same
      // instructions??
      if (i1->is(fir::InstrType::ReturnInstr) &&
          i2->is(fir::InstrType::ReturnInstr)) {
        if (i1->args.size() == 0 || i1->args[0] == i2->args[0]) {
          continue;
        } else if (i1->args[0].is_constant() && i2->args[0].is_constant()) {
          difference_values.push_back({i1, 0, i1->args[0], i2, 0});
          continue;
        }
      }
      if (bb1.bb->instructions[i] != bb2.bb->instructions[i]) {
        found = false;
        break;
      }
    }
    if (found) {
      res_bb1 = bb1.bb;
      res_bb2 = bb2.bb;
      break;
    }
  }

  if (found && difference_values.size() <= res_bb1->instructions.size()) {

    TVec<fir::BBArgument> new_bb_args;

    for (auto &diff : difference_values) {
      auto new_arg = ctx->storage.insert_bb_arg(
          {res_bb1, diff.instr->args[diff.arg_id].get_type()});
      res_bb1.add_arg(new_arg);
      diff.instr.replace_arg(diff.arg_id, fir::ValueR{new_arg});
    }

    for (auto u1 : res_bb1->uses) {
      ASSERT(u1.type == fir::UseType::BB);
      auto &bb_ref = u1.user->bbs[u1.argId];
      for (auto &diff : difference_values) {
        bb_ref.args.push_back(diff.old_val);
      }
    }
    for (auto u2 : res_bb2->uses) {
      ASSERT(u2.type == fir::UseType::BB);
      auto &bb_ref = u2.user->bbs[u2.argId];
      for (auto &diff : difference_values) {
        bb_ref.args.push_back(diff.instr2->args[diff.arg_id2]);
      }
    }
    res_bb2->replace_all_uses(fir::ValueR(res_bb1));
    ASSERT(res_bb2->get_n_uses() == 0);
    res_bb2->remove_from_parent(true, true);
    return true;
  }

  return false;
}

bool SimplifyCFG::remove_useless_bb_args(CFG &cfg, CFG::Node &curr,
                                         fir::Function & /*func*/,
                                         size_t /*bb_id*/, bool /*is_entry*/) {
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
    return true;
  }
  return false;
}

bool SimplifyCFG::remove_constant_bb_args(CFG & /*cfg*/, CFG::Node &curr,
                                          fir::Function & /*func*/,
                                          size_t /*bb_id*/, bool is_entry) {
  if (curr.bb->n_args() != 0 && !is_entry) {
    auto n_args = curr.bb->n_args();
    for (u32 ip1 = n_args; ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      fir::ValueR c_value{};
      bool is_c = true;

      TVec<fir::Use> uses{curr.bb->uses.begin(), curr.bb->uses.end()};

      for (auto use : uses) {
        ASSERT(use.type == fir::UseType::BB);
        auto pred_term = use.user;
        auto pred_term_bb_id = use.argId;
        auto incoming_arg = pred_term->bbs[pred_term_bb_id].args[i];
        if (c_value.is_invalid() || incoming_arg.eql(c_value)) {
          c_value = incoming_arg;
        } else if (incoming_arg.is_bb_arg() &&
                   incoming_arg.as_bb_arg() == curr.bb->args[i]) {
        } else {
          is_c = false;
          break;
        }
      }
      if (is_c) {
        for (auto use : uses) {
          ASSERT(use.type == fir::UseType::BB);
          auto user = use.user;
          auto bb_id = user.get_bb_id(curr.bb);
          user.remove_bb_arg(bb_id, i);
        }
        curr.bb->args[i]->replace_all_uses(c_value);
        curr.bb->args.erase(curr.bb->args.begin() + i);
        return true;
      }
    }
  }
  return false;
}

bool SimplifyCFG::distribute_return(CFG &cfg, CFG::Node &curr,
                                    fir::Function & /*func*/, size_t /*bb_id*/,
                                    bool /*is_entry*/) {
  if (curr.bb->n_instrs() == 1 &&
      curr.bb->get_terminator()->is(fir::InstrType::ReturnInstr)) {
    const auto n_args = curr.bb->n_args();
    const auto return_instr = curr.bb->get_terminator();
    TMap<fir::ValueR, fir::ValueR> subs{};
    bool modified_any = false;
    for (auto pred_id : curr.pred) {
      auto &pred = cfg.bbrs[pred_id];
      auto prev_term = pred.bb->get_terminator();
      if (pred.succ.size() != 1) {
        continue;
      }
      modified_any = true;
      subs.clear();
      for (size_t bb_arg_id = 0; bb_arg_id < n_args; bb_arg_id++) {
        subs.insert({fir::ValueR{curr.bb->args[bb_arg_id]},
                     fir::ValueR{prev_term->bbs[0].args[bb_arg_id]}});
      }

      fir::Builder bb{pred.bb};
      bb.at_end(pred.bb);

      auto new_return = bb.insert_copy(return_instr);
      new_return.substitute(subs);
      prev_term.remove_from_parent();
    }
    return modified_any;
  }
  return false;
}

bool SimplifyCFG::merge_empty_block_backwards(CFG &cfg, CFG::Node &curr,
                                              fir::Function &func, size_t bb_id,
                                              bool /*is_entry*/) {
  if (curr.succ.size() != 1) {
    return false;
  }

  auto succ = cfg.bbrs[curr.succ[0]];
  // TODO: this setup leads to infinite replcements
  //  0x5591b4d37910():
  //    0x5591b4db2948 : () = Branch<0x5591b4d379c0()>(){}
  //  0x5591b4d37968():
  //    0x5591b4d32ba8 : () = Branch<0x5591b4d379c0()>(){}
  //  0x5591b4d379c0():
  //    0x5591b4d32998 : () = Branch<0x5591b4d37968()>(){}

  if (succ.bb->n_instrs() == 1 && succ.bb->n_args() == 0) {
    auto old_term = func.basic_blocks[bb_id]->get_terminator();

    fir::Builder bb{curr.bb};
    bb.at_end(curr.bb);

    bb.insert_copy(succ.bb->get_terminator());

    old_term.remove_from_parent();
    if (succ.pred.size() == 1) {
      succ.bb->remove_from_parent(true);
    }
    return true;
  }
  return false;
}

bool SimplifyCFG::merge_empty_block_forwards(CFG &cfg, CFG::Node &curr,
                                             fir::Function &func, size_t bb_id,
                                             bool is_entry) {
  if (curr.bb->n_instrs() == 1 &&
      curr.bb->get_terminator()->is(fir::InstrType::BranchInstr)) {
    ASSERT(curr.succ.size() == 1);
    auto succ = cfg.bbrs[curr.succ[0]].bb;
    // if no bb args involved just replace
    if (curr.bb->n_args() == 0 && succ->n_args() == 0 &&
        (!is_entry ||
         (cfg.bbrs[curr.succ[0]].pred.size() == 1 && curr.succ[0] == 1))) {
      curr.bb->replace_all_uses(fir::ValueR(succ));
      curr.bb->remove_from_parent(true);
      return true;
    }
    // if this block doesnt have bb_args we might be able to replace all
    // incoming edges with the outgoig jump + args
    // TODO: handle entry
    if (curr.bb->n_args() == 0 && !is_entry) {
      const auto &terminator_args = curr.bb->get_terminator()->bbs[0].args;
      TVec<fir::Use> uses(curr.bb->get_uses().begin(),
                          curr.bb->get_uses().end());
      for (const auto &use : uses) {
        ASSERT(use.type == fir::UseType::BB);
        auto bb_id = use.argId;
        auto user = use.user;
        ASSERT(user->bbs[bb_id].args.size() == 0);
        user.replace_bb(bb_id, succ, false);

        // ASSERT(user->bbs[bb_id].args.size() == 0);
        for (auto old_arg : terminator_args) {
          user.add_bb_arg(bb_id, old_arg);
        }
        ASSERT(user->bbs[bb_id].args.size() == terminator_args.size());
      }
      ASSERT(curr.bb->get_n_uses() == 0);
      func.basic_blocks[bb_id]->remove_from_parent(true);
      return true;
    }
  }
  return false;
}

bool SimplifyCFG::merge_linear_relation(CFG &cfg, CFG::Node &curr,
                                        fir::Function &func, size_t bb_id,
                                        bool is_entry) {
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
    return true;
  }
  return false;
}

bool SimplifyCFG::conditional_to_cmove(CFG & /*cfg*/, CFG::Node &curr,
                                       fir::Function & /*func*/,
                                       size_t /*bb_id*/, bool /*is_entry*/) {
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
      new_inputs.push_back(
          bb.build_select(args1[i].get_type(), condition, args1[i], args2[i]));
    }
    auto new_branch = bb.build_branch(target);
    for (auto &input : new_inputs) {
      new_branch.add_bb_arg(0, input);
    }
    terminator.remove_from_parent();
    return true;
  }
  return false;
}

bool SimplifyCFG::simplify_cfg(CFG &cfg, fir::Function &func, size_t bb_id) {
  auto &curr = cfg.bbrs[bb_id];
  bool is_entry = bb_id == cfg.entry;

  if (remove_dead_bb(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (remove_dead_bb_arg(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (remove_useless_bb_args(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (remove_constant_bb_args(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (distribute_return(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (merge_empty_block_backwards(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (merge_empty_block_forwards(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (merge_linear_relation(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (conditional_to_cmove(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  if (dup_bb_to_args(cfg, curr, func, bb_id, is_entry)) {
    return true;
  }

  return false;
}

void SimplifyCFG::apply(fir::Context & /*unused*/, fir::Function &func) {
  ZoneScopedN("SimplifyCFG");
  CFG cfg{func};

  auto iter = 0;
  bool modified = true;
  while (modified) {
    modified = false;
    for (size_t bb_id = 1; bb_id <= cfg.bbrs.size(); bb_id++) {
      if (simplify_cfg(cfg, func, bb_id - 1)) {
        cfg.update(func, false);
        modified = true;
      }
    }
    if (iter++ > 100) {
      failure({"Didnt converge fixme\n", func.basic_blocks[0]});
      break;
    }
  }
  ASSERT(func.verify());
}

} // namespace foptim::optim
