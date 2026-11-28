#include "optim/func_passes/loop_unswitch.hpp"

#include <fmt/base.h>

#include <algorithm>

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
  if (std::ranges::find(info.body_nodes, info.head) == info.body_nodes.end()) {
    return false;
  }
  // if theres jsut 2 node we cant really have a proper if
  const bool can_have_if = info.body_nodes.size() > 2;
  auto is_invariant = [&](fir::ValueR v) {
    u32 def_bb = 0;
    if (v.is_instr()) {
      def_bb = cfg.get_bb_id(v.as_instr()->get_parent());
    } else if (v.is_bb_arg()) {
      def_bb = cfg.get_bb_id(v.as_bb_arg()->get_parent());
    } else {
      return false;
    }
    return std::ranges::find(info.body_nodes, def_bb) == info.body_nodes.end();
  };
  // duplicates the loop, specialise(builder, copy) then fixes the condition in
  // the original (runs with it true) and in the copy (false)
  auto unswitch_on = [&](fir::ValueR target_cond, auto &&specialise,
                         bool specialise_after_merge = false) -> bool {
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
      auto collect = [&](fir::ValueR val, u32 def_node) {
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
          // the value has to reach every edge into the exit, which only holds
          // when its block dominates the exit target
          if (exit == ~0U || (found_exit && exit != ua.exit_bb) ||
              !dom.dominates(def_node, exit)) {
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
          ok = ok && collect(fir::ValueR{arg}, node);
        }
        for (auto instr : cfg.bbrs[node].bb->instructions) {
          if (instr.is_multi_result()) {
            for (u8 r = 0; r < instr->n_results(); r++) {
              ok = ok && collect(instr.result(r), node);
            }
          } else {
            ok = ok && collect(instr.result(), node);
          }
        }
      }
      // exit targets must only be entered from the loop
      for (auto ua : used_after) {
        for (auto pred : cfg.bbrs[ua.exit_bb].pred) {
          ok = ok && in_loop(pred);
        }
      }
      if (!ok) {
        return false;
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

    if (!specialise_after_merge) {
      specialise(buh, copied_loop);
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

    // folding selects destroys loop values that can be used after the loop,
    // so it has to see the merged values
    if (specialise_after_merge) {
      specialise(buh, copied_loop);
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
  };
  for (auto bnode : info.body_nodes) {
    if (!can_have_if) {
      break;
    }
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

    // and / or with one invariant operand is unswitched on that operand
    enum class CondKind { Plain, And, Or };
    CondKind cond_kind = CondKind::Plain;
    fir::ValueR rest_cond;
    if (!is_invariant(target_cond)) {
      if (!target_cond.is_instr() ||
          !target_cond.as_instr()->is(fir::InstrType::BinaryInstr)) {
        continue;
      }
      auto bin = target_cond.as_instr();
      if (bin->is(fir::BinaryInstrSubType::And)) {
        cond_kind = CondKind::And;
      } else if (bin->is(fir::BinaryInstrSubType::Or)) {
        cond_kind = CondKind::Or;
      } else {
        continue;
      }
      if (!bin->result_type()->is_int() || bin->result_type()->as_int() != 1) {
        continue;
      }
      if (is_invariant(bin->args[0])) {
        target_cond = bin->args[0];
        rest_cond = bin->args[1];
      } else if (is_invariant(bin->args[1])) {
        target_cond = bin->args[1];
        rest_cond = bin->args[0];
      } else {
        continue;
      }
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

      // small loops are cheap to copy
      constexpr u32 MaxSmallLoopInstrs = 48;
      u32 loop_instrs = 0;
      for (auto n : info.body_nodes) {
        loop_instrs += cfg.bbrs[n].bb->n_instrs();
      }
      // TODO: do a better heuristic good enough for now
      if (loop_instrs > MaxSmallLoopInstrs &&
          duplicated_instr * 2 > saved_instr) {
        continue;
      }
    }

    if (unswitch_on(target_cond, [&](fir::Builder &buh,
                                     TVec<fir::BasicBlock> &copied_loop) {
          // the original runs with the condition true, the copy with it false
          auto replace_with_branch = [&](fir::BasicBlock bb, u32 target) {
            auto term = bb->get_terminator();
            buh.at_end(bb);
            auto branch = buh.build_branch(term->bbs[target].bb);
            for (auto arg : term->bbs[target].args) {
              branch.add_bb_arg(0, arg);
            }
            term.destroy();
          };
          u32 copied_target_bb_id = 0;
          for (size_t i = 0; i < info.body_nodes.size(); i++) {
            if (cfg.bbrs[info.body_nodes[i]].bb == target_bb) {
              copied_target_bb_id = i;
              break;
            }
          }
          auto copied_target_bb = copied_loop[copied_target_bb_id];
          auto copied_rest = [&]() {
            auto it = help.map.find(rest_cond);
            return it == help.map.end() ? rest_cond : it->second;
          };
          switch (cond_kind) {
            case CondKind::Plain:
              replace_with_branch(target_bb, 0);
              replace_with_branch(copied_target_bb, 1);
              break;
            case CondKind::And:
              target_term.replace_arg(0, rest_cond);
              replace_with_branch(copied_target_bb, 1);
              break;
            case CondKind::Or:
              replace_with_branch(target_bb, 0);
              copied_target_bb->get_terminator().replace_arg(0, copied_rest());
              break;
          }
        })) {
      return true;
    }
  }

  // a select on a loop invariant condition, the branch was already turned into
  // one by SimplifyCFG. Worth it when an arm computes something expensive that
  // the other version of the loop does not run.
  auto expensive = [](fir::Instr instr) {
    return instr->is(fir::BinaryInstrSubType::FloatDiv) ||
           instr->is(fir::UnaryInstrSubType::FloatSqrt) ||
           instr->is(fir::InstrType::CallInstr);
  };
  // instructions of the loop that only feed this arm of the select, how many
  // there are and if one of them is expensive
  auto arm_cone = [&](fir::Instr sel, u32 arm, u32 &n_instrs, bool &heavy) {
    TVec<fir::Instr> cone;
    TVec<fir::ValueR> work = {sel->args[arm]};
    auto in_loop = [&](fir::ValueR v) {
      return v.is_instr() && !is_invariant(v) &&
             !v.as_instr()->is(fir::InstrType::AllocaInstr);
    };
    while (!work.empty()) {
      auto v = work.back();
      work.pop_back();
      if (!in_loop(v) || std::ranges::find(cone, v.as_instr()) != cone.end()) {
        continue;
      }
      cone.push_back(v.as_instr());
      for (auto a : v.as_instr()->args) {
        work.push_back(a);
      }
    }
    // drop what is also used by something outside of the cone
    bool changed = true;
    while (changed) {
      changed = false;
      for (size_t i = 0; i < cone.size(); i++) {
        auto instr = cone[i];
        if (!instr->has_one_result()) {
          cone.erase(cone.begin() + i);
          changed = true;
          break;
        }
        bool inside = true;
        for (auto use : *instr.result().get_uses()) {
          if (use.user == sel) {
            inside = sel->args[arm] == instr.result() &&
                     sel->args[3 - arm] != instr.result();
          } else {
            inside = std::ranges::find(cone, use.user) != cone.end();
          }
          if (!inside) {
            break;
          }
        }
        if (!inside) {
          cone.erase(cone.begin() + i);
          changed = true;
          break;
        }
      }
    }
    n_instrs = cone.size();
    heavy = std::ranges::any_of(cone, expensive);
  };
  u32 loop_instrs = 0;
  for (auto n : info.body_nodes) {
    loop_instrs += cfg.bbrs[n].bb->n_instrs();
  }
  constexpr u32 MaxSmallLoopInstrs = 48;
  constexpr u32 MinConeInstrs = 4;
  for (auto n : info.body_nodes) {
    for (auto sel : cfg.bbrs[n].bb->get_instrs()) {
      if (!sel->is(fir::InstrType::SelectInstr) || !sel->has_one_result()) {
        continue;
      }
      // the condition, or one operand of an and / or condition, is invariant
      enum class Kind { Plain, And, Or };
      Kind kind = Kind::Plain;
      fir::ValueR inv_cond = sel->args[0];
      fir::ValueR rest_cond;
      if (!is_invariant(inv_cond)) {
        if (!inv_cond.is_instr() ||
            !inv_cond.as_instr()->is(fir::InstrType::BinaryInstr) ||
            !inv_cond.get_type()->is_int() ||
            inv_cond.get_type()->as_int() != 1) {
          continue;
        }
        auto bin = inv_cond.as_instr();
        if (bin->is(fir::BinaryInstrSubType::And)) {
          kind = Kind::And;
        } else if (bin->is(fir::BinaryInstrSubType::Or)) {
          kind = Kind::Or;
        } else {
          continue;
        }
        if (is_invariant(bin->args[0])) {
          inv_cond = bin->args[0];
          rest_cond = bin->args[1];
        } else if (is_invariant(bin->args[1])) {
          inv_cond = bin->args[1];
          rest_cond = bin->args[0];
        } else {
          continue;
        }
      } else if (!inv_cond.get_type()->is_int() ||
                 inv_cond.get_type()->as_int() != 1) {
        continue;
      }
      bool heavy = false;
      u32 n_instrs = 0;
      for (u32 arm = 1; arm <= 2; arm++) {
        u32 n_arm = 0;
        bool heavy_arm = false;
        arm_cone(sel, arm, n_arm, heavy_arm);
        heavy |= heavy_arm;
        n_instrs = std::max(n_instrs, n_arm);
      }
      if (!heavy &&
          (n_instrs < MinConeInstrs || loop_instrs > MaxSmallLoopInstrs)) {
        continue;
      }
      auto sel_result = sel.result();
      auto specialise_select = [&](fir::Builder &, TVec<fir::BasicBlock> &) {
        auto copy = help.map.at(sel_result).as_instr();
        auto fold = [](fir::Instr s, u32 arm) {
          auto val = s->args[arm];
          s.result().replace_all_uses(val);
          s.destroy();
        };
        auto copied_rest = [&]() {
          auto it = help.map.find(rest_cond);
          return it == help.map.end() ? rest_cond : it->second;
        };
        switch (kind) {
          case Kind::Plain:
            fold(sel, 1);
            fold(copy, 2);
            break;
          case Kind::And:
            sel.replace_arg(0, rest_cond);
            fold(copy, 2);
            break;
          case Kind::Or:
            fold(sel, 1);
            copy.replace_arg(0, copied_rest());
            break;
        }
      };
      // the original runs with the condition true, the copy with it false
      if (unswitch_on(inv_cond, specialise_select, true)) {
        return true;
      }
    }
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
  bool modified = false;
  // every unswitch duplicates a loop, and the copies can be unswitched on
  // their other conditions again, so the growth of the function is bounded
  const size_t max_blocks = std::max<size_t>(cfg.bbrs.size() * 2, 64);
  for (auto loop = linfo.info.begin(); loop != linfo.info.end(); loop++) {
    if (cfg.bbrs.size() >= max_blocks) {
      break;
    }
    // earlier unswitches leave the dead arm of the folded branch behind, a
    // loop in there has no complete body
    if (!dom.is_reachable(loop->head)) {
      continue;
    }
    helper.reset(cfg);
    bool apply_res = apply(ctx, cfg, *loop, helper);
    modified |= apply_res;
    if (apply_res && linfo.info.size() > 1) {
      cfg.update(func, false);
      dom.update(cfg);
      linfo.update(dom);
      loop = linfo.info.begin();
    }
  }
  // fmt::println("{:cd}", func);
  // fmt::println("okak");
  return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
