#include "optim/func_passes/loop_unswitch.hpp"

#include <algorithm>
#include <fmt/base.h>

#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "utils/set.hpp"
namespace foptim::optim {

bool LoopUnswitch::apply(fir::Context &ctx, CFG &cfg, LoopInfo &info,
                         HelperData &help) {
  (void)ctx;
  (void)cfg;
  // if theres jsut 2 node we cant really have a proper if
  if (info.body_nodes.size() <= 2) {
    return false;
  }
  for (auto bnode : info.body_nodes) {
    // find all conditional bbs inside the loop that arent exit conditions
    if (std::ranges::find(info.leaving_nodes, bnode) !=
        info.leaving_nodes.end()) {
      continue;
    }
    // TODO: could support switches aswell
    if (cfg.bbrs[bnode].succ.size() != 2) {
      continue;
    }
    auto target_bb = cfg.bbrs[bnode].bb;
    auto target_term = target_bb->get_terminator();
    auto target_cond = target_term->args[0];
    if (!target_cond.is_instr() && !target_cond.is_bb_arg()) {
      continue;
    }

    // figure out if the target cond is evaluated outside the loop -> loop
    // invariante
    u32 cond_bb = 0;
    if (target_cond.is_instr()) {
      cond_bb = cfg.get_bb_id(target_cond.as_instr()->get_parent());
    } else if (target_cond.is_bb_arg()) {
      cond_bb = cfg.get_bb_id(target_cond.as_bb_arg()->get_parent());
    } else {
      UNREACH();
    }
    if (std::ranges::find(info.body_nodes, cond_bb) != info.body_nodes.end()) {
      continue;
    }

    // split the loop into A CondIf CondElse C
    //  + also theres target_bb which might ocntain additional instructions
    //  which would be part of A
    // info.dump();

    // TODO: i got rid of actually spliting a/b/c so this is kinda useless
    // since its only used for the heuristic can prob calculate it directly
    // tho
    for (auto n : info.body_nodes) {
      help.a[n].set(true);
    }
    TVec<u32> worklist = {};
    TSet<u32> seen = {};
    {
      worklist.clear();
      seen.clear();
      worklist.push_back(cfg.get_bb_id(target_term->bbs[0].bb));
      while (!worklist.empty()) {
        auto curr = worklist.back();
        worklist.pop_back();
        if (seen.contains(curr)) {
          continue;
        }
        seen.insert(curr);
        help.condIf[curr].set(true);
        if (std::ranges::find(info.tails, curr) != info.tails.end()) {
          continue;
        }
        for (auto succ : cfg.bbrs[curr].succ) {
          worklist.push_back(succ);
        }
      }
      worklist.clear();
      seen.clear();
      worklist.push_back(cfg.get_bb_id(target_term->bbs[1].bb));
      while (!worklist.empty()) {
        auto curr = worklist.back();
        worklist.pop_back();
        if (seen.contains(curr)) {
          continue;
        }
        seen.insert(curr);
        help.condElse[curr].set(true);
        if (std::ranges::find(info.tails, curr) != info.tails.end()) {
          continue;
        }
        for (auto succ : cfg.bbrs[curr].succ) {
          worklist.push_back(succ);
        }
      }
    }

    help.c.assign(help.condIf).mul(help.condElse);
    help.condElse.mul_not(help.c);
    help.condIf.mul_not(help.c);
    help.a.mul_not(help.c).mul_not(help.condIf).mul_not(help.condElse);
    help.a[cfg.get_bb_id(target_bb)].set(false);
    // fmt::println(">>> {}", help.condIf);
    // if we get loop inside our loop and the condition inside that we cant
    // really do much i think for now? not sure so we for now filter them out
    // this should still cause it to be moved out of the inner loop and then
    // in a secdon application out of the outer but it would be nicer if it
    // was just 1 application
    {
      if (!help.condIf.any() && !help.condElse.any()) {
        continue;
      }

      // colllect all the data for da heuristic
      u32 duplicated_instr = (target_bb->n_instrs() + 1) * 2;
      // saved atleast 1 condition aswell
      u32 saved_instr = 1;

      for (auto bb : help.a) {
        duplicated_instr += cfg.bbrs[bb].bb->n_instrs();
      }
      for (auto bb : help.c) {
        duplicated_instr += cfg.bbrs[bb].bb->n_instrs();
      }
      for (auto bb : help.condIf) {
        saved_instr += cfg.bbrs[bb].bb->n_instrs();
      }
      for (auto bb : help.condElse) {
        saved_instr += cfg.bbrs[bb].bb->n_instrs();
      }

      // TODO: do a better heuristic good enough for now
      if (duplicated_instr * 2 > saved_instr) {
        continue;
      }
    }

    // values defined in the loop that are used after it: once the loop is
    // duplicated both exits reach the same outside block, so every such value
    // gets a new block argument on the exit target that dominates its uses
    struct UsedAfter {
      fir::ValueR val;
      u32 exit_bb;
      TVec<fir::Use> uses;
    };
    TVec<UsedAfter> used_after;
    {
      Dominators &dom = AnalysisManager::dom(*cfg.func);
      auto in_loop = [&](u32 id) {
        return std::ranges::find(info.body_nodes, id) != info.body_nodes.end();
      };
      TVec<u32> exit_targets;
      for (auto node : info.body_nodes) {
        for (auto succ : cfg.bbrs[node].succ) {
          if (!in_loop(succ) &&
              std::ranges::find(exit_targets, succ) == exit_targets.end()) {
            exit_targets.push_back(succ);
          }
        }
      }
      auto collect = [&](fir::ValueR val) {
        UsedAfter ua{.val = val, .exit_bb = 0, .uses = {}};
        bool found_exit = false;
        for (auto use : *val.get_uses()) {
          auto use_bb = cfg.get_bb_id(use.user->get_parent());
          if (in_loop(use_bb)) {
            continue;
          }
          u32 exit = ~0U;
          for (auto e : exit_targets) {
            if (dom.dominates(e, use_bb)) {
              exit = e;
              break;
            }
          }
          if (exit == ~0U || (found_exit && exit != ua.exit_bb)) {
            return false;
          }
          found_exit = true;
          ua.exit_bb = exit;
          ua.uses.push_back(use);
        }
        if (found_exit) {
          used_after.push_back(std::move(ua));
        }
        return true;
      };
      bool ok = true;
      for (auto node : info.body_nodes) {
        for (auto arg : cfg.bbrs[node].bb->args) {
          ok = ok && collect(fir::ValueR{arg});
        }
        for (auto instr : cfg.bbrs[node].bb->instructions) {
          ok = ok && collect(fir::ValueR{instr});
        }
      }
      // exit targets must only be entered from the loop
      for (auto ua : used_after) {
        for (auto pred : cfg.bbrs[ua.exit_bb].pred) {
          ok = ok && in_loop(pred);
        }
      }
      if (!ok) {
        continue;
      }
    }
    // save which node within the loop is the head so when copying the loop we
    // know which node is its head aswell
    u32 copied_head_bb_id = 0;
    for (size_t i = 0; i < info.body_nodes.size(); i++) {
      if (info.body_nodes[i] == info.head) {
        copied_head_bb_id = i;
        break;
      }
    }
    // cfg.func->append_bbr(fir::BasicBlock::ne)
    fir::Builder buh{cfg.func};

    // fmt::println("{:cd}", *cfg.func);

    TVec<fir::BasicBlock> copied_loop;
    help.map.clear();
    for (auto n : info.body_nodes) {
      // creates new copy
      auto copy = ctx->copy(cfg.bbrs[n].bb, help.map, false);
      copied_loop.push_back(copy);
      cfg.func->append_bbr(copy);
    }
    // we substitute after since loops order are a bit iffy if we do direct
    // substitution
    for (auto bb : copied_loop) {
      for (auto instr : bb->instructions) {
        instr.substitute(help.map);
      }
    }

    // doing the true if target
    {
      buh.at_end(target_bb);
      auto branch = buh.build_branch(target_term->bbs[0].bb);
      for (auto arg : target_term->bbs[0].args) {
        branch.add_bb_arg(0, arg);
      }
      target_term.destroy();
    }
    // doing the copied false target
    {
      // we however need to find out out of our copied bbs which one is our
      // target bb
      u32 copied_target_bb_id = 0;
      for (size_t i = 0; i < info.body_nodes.size(); i++) {
        if (cfg.bbrs[info.body_nodes[i]].bb == target_bb) {
          copied_target_bb_id = i;
          break;
        }
      }
      auto copied_target_bb = copied_loop[copied_target_bb_id];
      buh.at_end(copied_target_bb);
      auto copied_term = copied_target_bb->get_terminator();
      auto branch = buh.build_branch(copied_term->bbs[1].bb);
      for (auto arg : copied_term->bbs[1].args) {
        branch.add_bb_arg(0, arg);
      }
      copied_term.destroy();
    }

    // merge the used after values at the exit targets
    for (auto &ua : used_after) {
      auto exit_bb = cfg.bbrs[ua.exit_bb].bb;
      auto merged = exit_bb.add_arg(
          ctx->storage.insert_bb_arg(exit_bb, ua.val.get_type()));
      auto copied_val = help.map.at(ua.val);
      auto patch_edges = [&](fir::BasicBlock from, fir::ValueR val) {
        auto term = from->get_terminator();
        for (u32 i = 0; i < term->bbs.size(); i++) {
          if (term->bbs[i].bb == exit_bb) {
            term.add_bb_arg(i, val);
          }
        }
      };
      for (auto node : info.body_nodes) {
        patch_edges(cfg.bbrs[node].bb, ua.val);
      }
      for (auto bb : copied_loop) {
        patch_edges(bb, copied_val);
      }
      for (auto use : ua.uses) {
        use.replace_use(fir::ValueR{merged});
      }
    }

    {
      // insert our new condition
      auto new_outer_cond_bb = buh.append_bb();
      buh.at_end(new_outer_cond_bb);
      auto cond_branch = buh.build_cond_branch(
          target_cond, cfg.bbrs[info.head].bb, copied_loop[copied_head_bb_id]);
      // then we need to updated all the incoming branches so they now point
      // to our new condition if our head takes bb args we need to find them
      // from the incoming branches so we can forward them
      auto head_bb = cfg.bbrs[info.head];
      if (!head_bb.bb->args.empty()) {
        for (auto arg : head_bb.bb->args) {
          auto copy = ctx->copy(arg);
          new_outer_cond_bb.add_arg(copy);
          cond_branch.add_bb_arg(0, fir::ValueR{copy});
          cond_branch.add_bb_arg(1, fir::ValueR{copy});
        }
      }

      // reuse map to update the incoming stuff
      help.map.clear();
      help.map.insert({fir::ValueR{cfg.bbrs[info.head].bb},
                       fir::ValueR{new_outer_cond_bb}});
      for (auto pot_incoming : cfg.bbrs[info.head].pred) {
        // need to check that its not a backwards edge
        if (std::ranges::find(info.tails, pot_incoming) != info.tails.end()) {
          continue;
        }
        // then we need to forward the terminator to our new entry
        cfg.bbrs[pot_incoming].bb->get_terminator().substitute(help.map);
      }
    }

    // fmt::println("{:cd}", *cfg.func);
    // fmt::println(
    //     "Found conditional inside of loop that isnt the condition\n{:cd}",
    //     cfg.bbrs[bnode].bb);
    // fmt::println("{}", help.a);
    // fmt::println("{}", help.condIf);
    // fmt::println("{}", help.condElse);
    // fmt::println("{}", help.c);
    // fmt::println("Duplicated: {} Saved: {}", duplicated_instr,
    // saved_instr); info.dump();
    // TODO("impl");
    return true;
  }
  return false;
}

PreservedAnalysis LoopUnswitch::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("LoopUnswitch");
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  LoopInfoAnalysis linfo{dom};

  // fmt::println("TODO FIX LOOPUNSWITCH");
  // return PreservedAnalysis::all();
  auto helper = HelperData{
      .a = BitSet<>::empty(cfg.bbrs.size()),
      .condIf = BitSet<>::empty(cfg.bbrs.size()),
      .condElse = BitSet<>::empty(cfg.bbrs.size()),
      .c = BitSet<>::empty(cfg.bbrs.size()),
      .map = fir::ContextData::V2VMap{},
  };
  for (auto loop = linfo.info.begin(); loop != linfo.info.end(); loop++) {
    helper.reset(cfg);
    bool apply_res = apply(ctx, cfg, *loop, helper);
    if (apply_res && linfo.info.size() > 1) {
      cfg.update(func, false);
      dom.update(cfg);
      linfo.update(dom);
      loop = linfo.info.begin();
    }
  }
  // fmt::println("{:cd}", func);
  // fmt::println("okak");
  return PreservedAnalysis::none();
}

} // namespace foptim::optim
