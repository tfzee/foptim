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
    fir::Instr new_instr = bb.insert_copy(instr, fir::MetaCopy::Types);
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
}  // namespace

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
  // the header has to be an exiting node, other exits in the body stay
  if (!std::ranges::contains(linfo.leaving_nodes, linfo.head)) {
    failure({.reason = "Header does not leave the loop ",
             .loc = {cfg.bbrs[linfo.head].bb}});
    return false;
  }
  const bool other_exits = linfo.leaving_nodes.size() > 1;
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
  // rotation copies the header once in front of the loop. small headers always
  // pay, a bigger one (row addresses LICM moved into the header of an outer
  // loop) only when the copy is at most half of the loop
  {
    constexpr size_t AlwaysRotate = 10;
    constexpr size_t MaxCopied = 48;
    const size_t header_size = header_bb->instructions.size();
    if (header_size > AlwaysRotate) {
      size_t loop_size = 0;
      for (const u32 b : linfo.body_nodes) {
        loop_size += cfg.bbrs[b].bb->instructions.size();
      }
      if (header_size > MaxCopied || header_size * 2 > loop_size) {
        failure({.reason = "Header to big ", .loc = {cfg.bbrs[linfo.head].bb}});
        return false;
      }
    }
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
  auto in_body = [&](const fir::Use &use) {
    return std::ranges::contains(linfo.body_nodes,
                                 cfg.get_bb_id(use.user->get_parent()));
  };
  // header values that the loop body uses stay available as extra arguments of
  // the header, values used behind the loop are merged over all exits
  TVec<fir::Instr> carried;
  bool used_after_instr = false;
  for (auto instr : header_bb->instructions) {
    bool used_in_body = false;
    for (auto use : instr->uses) {
      if (use.user->parent == header_bb) {
        // the arguments of the edge into the body are rebuilt from the
        // surviving header, so they count as body uses
        used_in_body |=
            use.type == fir::UseType::BBArg && use.argId == non_exiting_target;
        continue;
      }
      if (!in_body(use)) {
        used_after_instr = true;
        // the other exits pass the value on, so it has to survive the header
        used_in_body |= other_exits;
        continue;
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

  bool args_used_after = false;
  for (auto &arg : header_bb->args) {
    for (auto &use : arg->uses) {
      if (!in_body(use)) {
        args_used_after = true;
      }
    }
  }
  // the exits of the body have to meet the exit of the header in one block
  // that merges the header values used behind the loop
  if (other_exits && (used_after_instr || args_used_after)) {
    const auto exit_bb =
        header_bb->get_terminator()->bbs[1 - non_exiting_target].bb;
    for (const u32 n : linfo.leaving_nodes) {
      if (n == linfo.head) {
        continue;
      }
      auto term = cfg.bbrs[n].bb->get_terminator();
      for (u16 i = 0; i < term->bbs.size(); i++) {
        const auto t = term->bbs[i].bb;
        const bool leaves =
            !std::ranges::contains(linfo.body_nodes, cfg.get_bb_id(t));
        if (leaves && t != exit_bb) {
          failure({.reason = "Exits go to different blocks and header values "
                             "are used later",
                   .loc = {cfg.bbrs[linfo.head].bb}});
          return false;
        }
      }
    }
  }

  // a block that takes the arguments of the header and jumps to it, the edges
  // of the given blocks into the header go to it instead
  auto merge_edges = [&](const TVec<u32> &from) {
    auto merged = fir::Builder(header_bb).append_bb();
    for (auto arg : header_bb->args) {
      merged.add_arg(ctx->storage.insert_bb_arg(merged, arg->get_type()));
    }
    fir::Builder mb{merged};
    mb.at_end(merged);
    auto jmp = mb.build_branch(header_bb);
    for (auto arg : merged->args) {
      jmp.add_bb_arg(0, fir::ValueR{arg});
    }
    for (auto pred : from) {
      auto term = cfg.bbrs[pred].bb->get_terminator();
      for (u16 i = 0; i < term->bbs.size(); i++) {
        if (term->bbs[i].bb == header_bb) {
          term.replace_bb(i, merged, true);
        }
      }
    }
    return merged;
  };

  TVec<u32> outside_preds;
  for (auto pred : cfg.bbrs[linfo.head].pred) {
    if (!std::ranges::contains(linfo.tails, pred) &&
        !std::ranges::contains(outside_preds, pred)) {
      outside_preds.push_back(pred);
    }
  }
  TVec<fir::BasicBlock> head_preds;
  if (outside_preds.size() > 1) {
    // one copy of the header in a new preheader instead of one per entry
    head_preds.push_back(merge_edges(outside_preds));
  } else {
    for (auto pred : outside_preds) {
      head_preds.push_back(cfg.bbrs[pred].bb);
    }
  }
  if (linfo.tails.size() == 1) {
    head_preds.push_back(cfg.bbrs[linfo.tails[0]].bb);
  }
  if (linfo.tails.size() > 1) {
    // Several back edges would each get their own copy of the header and so
    // their own exit test. Merge them into one block first, the copy of the
    // header makes it the single latch of the rotated loop.
    head_preds.push_back(merge_edges(linfo.tails));
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

  TVec<fir::ValueR> used_after_vals;
  for (auto &arg : header_bb->args) {
    for (auto &use : arg->uses) {
      if (!in_body(use)) {
        used_after_vals.push_back(fir::ValueR(arg));
        break;
      }
    }
  }
  for (auto instr : header_bb->instructions) {
    for (auto &use : instr->uses) {
      if (use.user->parent != header_bb && !in_body(use)) {
        used_after_vals.push_back(instr.result());
        break;
      }
    }
  }

  if (!used_after_vals.empty()) {
    // a block in front of the exit that takes the old exit args plus the
    // header values that are needed later, all exits of the loop go through it
    auto old_header = cfg.bbrs[linfo.head].bb;
    auto bb = fir::Builder(old_header);
    auto old_terminator = old_header->get_terminator();

    ASSERT(old_terminator->is(fir::InstrType::CondBranchInstr));

    const auto exit_bb = old_terminator->bbs[exiting_target].bb;
    auto exit_args = old_terminator->bbs[exiting_target].args;

    auto new_exit = bb.append_bb();
    bb.at_end(new_exit);
    auto new_exit_term = bb.build_branch(exit_bb);

    for (auto argy : exit_args) {
      auto new_bb_arg = ctx->storage.insert_bb_arg(new_exit, argy.get_type());
      new_exit.add_arg(new_bb_arg);
      new_exit_term.add_bb_arg(0, fir::ValueR{new_bb_arg});
    }
    TVec<fir::ValueR> new_args;
    for (auto used_after : used_after_vals) {
      auto new_arg = new_exit.add_arg(
          ctx->storage.insert_bb_arg(new_exit, used_after.get_type()));
      new_args.push_back(fir::ValueR(new_arg));
    }

    // the other exits of the body, instruction values are swapped for the
    // header arguments that carry them by the loop below
    for (const u32 n : linfo.leaving_nodes) {
      if (n == linfo.head) {
        continue;
      }
      auto term = cfg.bbrs[n].bb->get_terminator();
      for (u16 i = 0; i < term->bbs.size(); i++) {
        if (term->bbs[i].bb != exit_bb) {
          continue;
        }
        term.replace_bb(i, new_exit, true);
        for (auto v : used_after_vals) {
          term.add_bb_arg(i, v);
        }
      }
    }

    old_terminator.replace_bb(exit_bb, new_exit, true);
    for (auto v : used_after_vals) {
      old_terminator.add_bb_arg(exiting_target, v);
    }

    for (u32 arg_id = 0; arg_id < used_after_vals.size(); arg_id++) {
      TVec<fir::Use> replaced_uses;
      auto uses = [&](auto &&u) {
        for (auto &use : u) {
          if (use.user->parent != old_header && !in_body(use)) {
            replaced_uses.push_back(use);
          }
        }
      };
      auto v = used_after_vals[arg_id];
      if (v.is_bb_arg()) {
        uses(v.as_bb_arg()->uses);
      } else {
        uses(v.as_instr()->uses);
      }
      for (auto use : replaced_uses) {
        use.replace_use(new_args[arg_id]);
      }
    }
  }

  for (auto c : carried) {
    auto arg = header_bb.add_arg(
        ctx->storage.insert_bb_arg(header_bb, c->result_type()));
    TVec<fir::Use> outside;
    for (auto use : c->uses) {
      if (use.user->parent != header_bb || (use.type == fir::UseType::BBArg &&
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

}  // namespace foptim::optim
