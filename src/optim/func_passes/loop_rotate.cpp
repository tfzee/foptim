#include "loop_rotate.hpp"

#include <algorithm>

#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/helper/helper.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

namespace {
void replace_branch_with_header(TMap<fir::ValueR, fir::ValueR> &repl_map,
                                uint8_t non_exiting_target,
                                fir::BasicBlock header_bb,
                                fir::BasicBlock head_pred,
                                const TVec<fir::Instr> &carried) {
  repl_map.clear();
  fir::Instr old_terminator = head_pred->get_terminator();
  auto &old_terminator_args = old_terminator->bbs[0].args;

  // head_pred->remove_instr();

  // we need the replacement for the bb args in case they are used in
  // the header
  {
    size_t arg_id = 0;
    for (auto arg : old_terminator_args) {
      repl_map.insert({fir::ValueR(header_bb->args[arg_id]), arg});
      arg_id++;
    }
  }
  fir::Builder bb{head_pred};
  bb.at_end(head_pred);

  // IDK about this
  // since they are just inserted at the end we could just use the length
  for (auto instr : header_bb->instructions) {
    fir::Instr new_instr = bb.insert_copy(instr);
    new_instr.substitute(repl_map);
    fir::map_results(instr, new_instr, repl_map);
  }

  // clean up the arguments going into the loop based on the args of the old
  // terminator
  {
    fir::Instr new_term = head_pred->get_terminator();

    new_term.replace_bb(non_exiting_target, header_bb, false);
    for (auto arg : old_terminator_args) {
      new_term.add_bb_arg(non_exiting_target, arg);
    }
    for (auto c : carried) {
      new_term.add_bb_arg(non_exiting_target, repl_map.at(c.result()));
    }
    flip_cond_branch(new_term);
  }

  old_terminator.remove_from_parent();
}
} // namespace

PreservedAnalysis LoopRotate::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("LoopRotate");
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  LoopInfoAnalysis linfo{dom};

  bool modified = false;
  for (auto loop = linfo.info.begin(); loop != linfo.info.end(); loop++) {
    bool apply_res = apply(ctx, cfg, *loop);
    modified |= apply_res;
    if (apply_res && linfo.info.size() > 1) {
      cfg.update(func, false);
      dom.update(cfg);
      linfo.update(dom);
      loop = linfo.info.begin();
    }
  }
  return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
}

bool LoopRotate::apply(fir::Context &ctx, const CFG &cfg, LoopInfo &linfo) {
  // only if the header is the only exiting node
  if (linfo.leaving_nodes.size() == 0) {
    // linfo.dump();
    failure({.reason = "No leaving node ", .loc = {cfg.bbrs[linfo.head].bb}});
    return false;
  }
  if (linfo.leaving_nodes.size() != 1 || linfo.leaving_nodes[0] != linfo.head) {
    failure({.reason = "Only leaving node can be the header ",
             .loc = {cfg.bbrs[linfo.head].bb}});
    return false;
  }
  auto header_bb = cfg.bbrs[linfo.head].bb;
  if (!header_bb->get_terminator()->is(fir::InstrType::CondBranchInstr)) {
    failure({.reason = "Header does not end in a conditional branch",
             .loc = {cfg.bbrs[linfo.head].bb}});
    return false;
  }
  // a header that jumps back to itself is already rotated
  if (std::ranges::contains(linfo.tails, linfo.head)) {
    failure({.reason = "Header is already the latch",
             .loc = {cfg.bbrs[linfo.head].bb}});
    return false;
  }
  // only worth it if the header aint to big
  // TODO: magic number parameter
  if (header_bb->instructions.size() > 10) {
    failure({.reason = "Header to big ", .loc = {cfg.bbrs[linfo.head].bb}});
    return false;
  }
  // which of the two targets of the header terminator is the non exiting one
  uint8_t non_exiting_target = 1;
  {
    auto first_target = header_bb->get_terminator()->bbs[0].bb;
    for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
      if (cfg.bbrs[bb_id].bb == first_target &&
          std::ranges::find(linfo.body_nodes, bb_id) !=
              linfo.body_nodes.end()) {
        non_exiting_target = 0;
        break;
      }
    }
  }
  // header values that the loop body uses stay available as extra arguments of
  // the header, values used behind the loop would need a merge of the exits
  TVec<fir::Instr> carried;
  for (auto instr : header_bb->instructions) {
    bool used_in_body = false;
    for (auto use : instr->uses) {
      if (use.user->parent == header_bb) {
        // the arguments of the edge into the body are rebuilt from the
        // surviving header, so they count as body uses
        used_in_body |= use.type == fir::UseType::BBArg &&
                        use.argId == non_exiting_target;
        continue;
      }
      if (!std::ranges::contains(linfo.body_nodes,
                                 cfg.get_bb_id(use.user->get_parent()))) {
        failure({.reason = "Header values are used later",
                 .loc = {cfg.bbrs[linfo.head].bb}});
        return false;
      }
      used_in_body = true;
    }
    if (instr->has_pot_sideeffects()) {
      failure({.reason = "Header has side effects ",
               .loc = {cfg.bbrs[linfo.head].bb}});
      return false;
    }
    if (used_in_body) {
      carried.push_back(instr);
    }
  }

  if (cfg.bbrs[linfo.head].pred.size() > 1 + linfo.tails.size()) {
    failure({.reason = "TODO: Impl preheader insertion",
             .loc = {cfg.bbrs[linfo.head].bb}});
    return false;
  }
  TVec<fir::BasicBlock> head_preds;
  for (auto pred : cfg.bbrs[linfo.head].pred) {
    head_preds.push_back(cfg.bbrs[pred].bb);
  }
  // every predecessor gets a copy of the header so it has to end in a plain
  // jump, split the other edges
  for (auto &pred : head_preds) {
    auto term = pred->get_terminator();
    if (term->is(fir::InstrType::BranchInstr)) {
      continue;
    }
    auto split = fir::Builder(header_bb).append_bb();
    for (auto arg : header_bb->args) {
      split.add_arg(ctx->storage.insert_bb_arg(split, arg->get_type()));
    }
    fir::Builder sb{split};
    sb.at_end(split);
    auto jmp = sb.build_branch(header_bb);
    for (auto arg : split->args) {
      jmp.add_bb_arg(0, fir::ValueR{arg});
    }
    for (u16 i = 0; i < term->bbs.size(); i++) {
      if (term->bbs[i].bb == header_bb) {
        term.replace_bb(i, split, true);
      }
    }
    pred = split;
  }

  uint8_t exiting_target = 1 - non_exiting_target;

  // if the header gets bbargs and these bb args are used *after* the loop
  //   we cant do a simple rotate since that might skip the loop leaving them
  //   undefined

  TVec<fir::BBArgument> used_after_args;
  for (auto &arg : header_bb->args) {
    for (auto &use : arg->uses) {
      auto user_bb_id = cfg.get_bb_id(use.user->get_parent());
      if (std::ranges::find(linfo.body_nodes, user_bb_id) ==
          linfo.body_nodes.end()) {
        used_after_args.push_back(arg);
        break;
      }
    }
  }

  if (!used_after_args.empty()) {
    // inserting en empty bb at the end which takes as bb args the header bb
    // args which need to be available later on
    //  and then replace all later uses of the bbargs with these new bbargs
    auto old_header = cfg.bbrs[linfo.head].bb;
    auto bb = fir::Builder(old_header);
    auto old_terminator = old_header->get_terminator();

    ASSERT(old_terminator->is(fir::InstrType::CondBranchInstr));

    auto &exit_target = old_terminator->bbs[exiting_target];

    auto new_exit = bb.append_bb();
    bb.at_end(new_exit);
    // add the terminator that forwards the old_exit_args
    auto new_exit_term = bb.build_branch(exit_target.bb);

    // contsruct our bb args
    //  BB(old_exit_args..., used_after_args...)
    // first the old args and forwarding them to the new termintor
    for (auto argy : exit_target.args) {
      auto new_bb_arg = ctx->storage.insert_bb_arg(new_exit, argy.get_type());
      new_exit.add_arg(new_bb_arg);
      new_exit_term.add_bb_arg(0, fir::ValueR{new_bb_arg});
    }
    // then the new args
    TVec<fir::BBArgument> new_args;
    for (auto used_after : used_after_args) {
      auto new_arg = new_exit.add_arg(
          ctx->storage.insert_bb_arg(new_exit, used_after->get_type()));
      new_args.push_back(new_arg);
    }

    // update the header terminator to point to new exit + add the
    // used_after_args as bbargs
    old_terminator.replace_bb(exit_target.bb, new_exit, true);
    for (auto used_after : used_after_args) {
      old_terminator.add_bb_arg(exiting_target, fir::ValueR(used_after));
    }

    for (u32 arg_id = 0; arg_id < used_after_args.size(); arg_id++) {
      auto arg = used_after_args[arg_id];
      TVec<fir::Use> replaced_uses;
      for (auto &use : arg->uses) {
        auto user_bb_id = cfg.get_bb_id(use.user->get_parent());
        if (std::ranges::find(linfo.body_nodes, user_bb_id) ==
            linfo.body_nodes.end()) {
          replaced_uses.push_back(use);
        }
      }
      for (auto use : replaced_uses) {
        use.replace_use(fir::ValueR(new_args[arg_id]));
      }
    }

    // now replace the uses after with the new_exits bb_args
    // failure({"Cannot handle loop rotate on loop whose header arguments"
    //          "are used after the loop",
    //          {(*used_after_args.begin().base())->get_parent()}});
    // return false;
  }

  for (auto c : carried) {
    auto arg = header_bb.add_arg(
        ctx->storage.insert_bb_arg(header_bb, c->result_type()));
    TVec<fir::Use> outside;
    for (auto use : c->uses) {
      if (use.user->parent != header_bb ||
          (use.type == fir::UseType::BBArg &&
           use.argId == non_exiting_target)) {
        outside.push_back(use);
      }
    }
    for (auto use : outside) {
      use.replace_use(fir::ValueR{arg});
    }
  }

  // TODO: prob want to replace this map since its never gonna be more then a
  // handful of bb_block args
  TMap<fir::ValueR, fir::ValueR> repl_map;
  // replace all incoming branch instructions with the full header
  {
    for (auto head_pred : head_preds) {
      replace_branch_with_header(repl_map, non_exiting_target, header_bb,
                                 head_pred, carried);
    }
  }

  // then clean up the original header and just forward a jump into the
  // internal edge
  {
    fir::BasicBlock header = cfg.bbrs[linfo.head].bb;
    fir::Builder bb{header};
    // assert(header->get_terminator()->bbs[non_exiting_target].args.empty());

    auto old_header_term = header->get_terminator();
    auto new_header_term =
        bb.build_branch(old_header_term->bbs[non_exiting_target].bb);
    for (auto arg : old_header_term->bbs[non_exiting_target].args) {
      new_header_term.add_bb_arg(0, arg);
    }
    while (header->instructions.size() > 1) {
      header->remove_instr(header->instructions.size() - 1, true);
    }
  }
  // linfo.dump();
  // std::abort();
  return true;
}

} // namespace foptim::optim
