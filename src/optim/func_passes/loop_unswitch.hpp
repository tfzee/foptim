#pragma once
#include <fmt/base.h>

#include <algorithm>

#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/value.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
#include "utils/set.hpp"

namespace foptim::optim {
class LoopUnswitch final : public FunctionPass {
 public:
  struct HelperData {
    BitSet<> a;
    BitSet<> condIf;
    BitSet<> condElse;
    BitSet<> c;
    fir::ContextData::V2VMap map;

    void reset(CFG &cfg) {
      if (cfg.bbrs.size() != a.bit_size()) {
        a = BitSet<>::empty(cfg.bbrs.size());
        condIf = BitSet<>::empty(cfg.bbrs.size());
        condElse = BitSet<>::empty(cfg.bbrs.size());
        c = BitSet<>::empty(cfg.bbrs.size());
      } else {
        a.reset(false);
        condIf.reset(false);
        condElse.reset(false);
        c.reset(false);
      }
      map.clear();
    }
  };
  bool apply(fir::Context &ctx, CFG &cfg, LoopInfo &info, HelperData &help) {
    (void)ctx;
    (void)cfg;
    // if theres jsut 2 node we cant really have a proper if
    if (info.body_nodes.size() <= 2) {
      return false;
    }
    for (auto bnode : info.body_nodes) {
      // find all conditional bbs inside the loop that arent exit conditions
      if (std::find(info.leaving_nodes.begin(), info.leaving_nodes.end(),
                    bnode) != info.leaving_nodes.end()) {
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
      if (std::find(info.body_nodes.begin(), info.body_nodes.end(), cond_bb) !=
          info.body_nodes.end()) {
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
          if (std::find(info.tails.begin(), info.tails.end(), curr) !=
              info.tails.end()) {
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
          if (std::find(info.tails.begin(), info.tails.end(), curr) !=
              info.tails.end()) {
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
        u32 duplicated_instr = target_bb->n_instrs() - 1;
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

      // sadly we also need to keep track if there are uses of values after the
      // loop since when we duplicate the loop we would need to merge them into
      // a new bb
      TVec<fir::ValueR> values_that_are_used_after;
      for (auto node : info.body_nodes) {
        for (auto arg : cfg.bbrs[node].bb->args) {
          for (auto use : arg->get_uses()) {
            auto use_bb = cfg.get_bb_id(use.user->get_parent());
            auto is_use_outside =
                std::find(info.body_nodes.begin(), info.body_nodes.end(),
                          use_bb) == info.body_nodes.end();
            if (is_use_outside) {
              values_that_are_used_after.push_back(fir::ValueR{arg});
              break;
            }
          }
        }
        for (auto instr : cfg.bbrs[node].bb->instructions) {
          for (auto use : instr->get_uses()) {
            auto use_bb = cfg.get_bb_id(use.user->get_parent());
            auto is_use_outside =
                std::find(info.body_nodes.begin(), info.body_nodes.end(),
                          use_bb) == info.body_nodes.end();
            if (is_use_outside) {
              values_that_are_used_after.push_back(fir::ValueR{instr});
              break;
            }
          }
        }
      }
      if (!values_that_are_used_after.empty()) {
        fmt::println("-->--{}", values_that_are_used_after.size());
        fmt::println("IMPLEMENT THIS LOOPUNSWITCH WITH USES AFTER");
        continue;
      }
      // save which node within the loop is the head so when copying the loop we
      // know which node is its head aswell
      auto copied_head_bb_id = 0;
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
        auto copied_target_bb_id = 0;
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

      {
        // insert our new condition
        auto new_outer_cond_bb = buh.append_bb();
        buh.at_end(new_outer_cond_bb);
        auto cond_branch =
            buh.build_cond_branch(target_cond, cfg.bbrs[info.head].bb,
                                  copied_loop[copied_head_bb_id]);
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
          if (std::find(info.tails.begin(), info.tails.end(), pot_incoming) !=
              info.tails.end()) {
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

  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("LoopUnswitch");
    CFG cfg{func};
    Dominators dom{cfg};
    LoopInfoAnalysis linfo{dom};

    auto helper = HelperData{
        .a = BitSet<>::empty(cfg.bbrs.size()),
        .condIf = BitSet<>::empty(cfg.bbrs.size()),
        .condElse = BitSet<>::empty(cfg.bbrs.size()),
        .c = BitSet<>::empty(cfg.bbrs.size()),
        .map = fir::ContextData::V2VMap{},
    };
    // fmt::println("BEFORE:");
    // fmt::println("{:cd}", func);
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
    // fmt::println("AFTER:");
    // fmt::println("{:cd}", func);
    // fmt::println("okak");
  }
};

}  // namespace foptim::optim
