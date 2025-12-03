#pragma once
#include <fmt/core.h>

#include <algorithm>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
#include "utils/set.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {
class LoopSimplify final : public FunctionPass {
 public:
  bool dead_loop_elimination(CFG &cfg, LoopInfoAnalysis &loops, u32 loop_i) {
    auto &loop = loops.info.at(loop_i);
    for (auto b : loop.body_nodes) {
      for (auto arg : cfg.bbrs[b].bb->args) {
        for (auto u : arg->uses) {
          auto use_bb = cfg.get_bb_id(u.user->get_parent());
          if (u.type == fir::UseType::BBArg) {
            if (!std::ranges::contains(
                    loop.body_nodes, cfg.get_bb_id(u.user->bbs[u.argId].bb))) {
              return false;
            }
          } else if (!std::ranges::contains(loop.body_nodes, use_bb)) {
            return false;
          }
        }
      }
      for (auto i : cfg.bbrs[b].bb->instructions) {
        if (i->has_pot_sideeffects()) {
          return false;
        }
        for (auto u : i->uses) {
          auto use_bb = cfg.get_bb_id(u.user->get_parent());
          if (u.type == fir::UseType::BBArg) {
            if (!std::ranges::contains(
                    loop.body_nodes, cfg.get_bb_id(u.user->bbs[u.argId].bb))) {
              return false;
            }
          } else if (!std::ranges::contains(loop.body_nodes, use_bb)) {
            return false;
          }
        }
      }
    }
    if (loop.leaving_nodes.size() == 1) {
      auto leaver_id = loop.leaving_nodes[0];
      auto leaver_term = cfg.bbrs[leaver_id].bb->get_terminator();
      if (!leaver_term->is(fir::InstrType::CondBranchInstr)) {
        return false;
      }
      fir::Builder bb{leaver_term};
      auto leave_index = 0;
      if (std::ranges::contains(loop.body_nodes,
                                cfg.get_bb_id(leaver_term->bbs[0].bb))) {
        leave_index = 1;
      }
      auto new_branch = bb.build_branch(leaver_term->bbs[leave_index].bb);
      for (auto a : leaver_term->bbs[leave_index].args) {
        new_branch.add_bb_arg(0, a);
      }
      leaver_term.destroy();
      utils::StatCollector::get().addi(1, "DeadLoopElim",
                                       utils::StatCollector::StatFOptim);
      return true;
    }
    return false;
  }

  bool propagate_to_after_loop(CFG &cfg, LoopInfoAnalysis &loops, u32 loop_i) {
    // propagate side effect free expressions that depend on other expresions
    auto &loop = loops.info[loop_i];
    TSet<fir::BasicBlock> body_nodes_lut;
    for (auto b_id : loop.body_nodes) {
      body_nodes_lut.insert(cfg.bbrs[b_id].bb);
    }
    for (auto b_id : loop.body_nodes) {
      for (auto instr : cfg.bbrs[b_id].bb->instructions) {
        if (instr->has_pot_sideeffects() || instr->is_critical()) {
          continue;
        }
        TVec<fir::Use> uses;
        for (auto use : instr->uses) {
          if (body_nodes_lut.contains(use.user->get_parent())) {
            continue;
          }
          uses.push_back(use);
        }
        for (auto use : uses) {
          fir::Builder b{use.user};
          auto copied = b.insert_copy(instr);
          use.replace_use(fir::ValueR{copied});
          utils::StatCollector::get().addi(1, "PropagateUseAfter",
                                           utils::StatCollector::StatFOptim);
        }
      }
    }
    return false;
  }

  bool merge_same_diret_induct_var(CFG &cfg, LoopInfoAnalysis &loops,
                                   u32 loop_i) {
    // if we have same direct induct var  with same ste and same starting valuep
    //  we can merge them
    //  if same size just merge otherwise need a itrunc to smaller
    auto &loop = loops.info[loop_i];
    if (cfg.bbrs[loop.head].pred.size() != loop.tails.size() + 1) {
      return false;
    }

    u32 prehead_bb = 0;
    fir::Instr prehead_bb_term;
    u32 prehead_bb_id = 0;
    for (auto pred : cfg.bbrs[loop.head].pred) {
      if (std::ranges::find(loop.body_nodes, pred) == loop.body_nodes.end()) {
        prehead_bb = pred;
        prehead_bb_term = cfg.bbrs[prehead_bb].bb->get_terminator();
        prehead_bb_id = prehead_bb_term.get_bb_id(cfg.bbrs[loop.head].bb);
        break;
      }
    }
    auto *ctx = cfg.func->ctx;

    InductionVarAnalysis ianal{cfg, loop};
    for (size_t i1 = 0; i1 < ianal.direct_inductvars.size(); i1++) {
      for (size_t i2 = i1 + 1; i2 < ianal.direct_inductvars.size(); i2++) {
        ASSERT(ianal.direct_inductvars[i1].def.is_bb_arg())
        ASSERT(ianal.direct_inductvars[i2].def.is_bb_arg())
        ASSERT(cfg.bbrs[loop.head].bb ==
               ianal.direct_inductvars[i1].def.as_bb_arg()->get_parent());
        ASSERT(cfg.bbrs[loop.head].bb ==
               ianal.direct_inductvars[i2].def.as_bb_arg()->get_parent());
        if (ianal.direct_inductvars[i1].type !=
            ianal.direct_inductvars[i2].type) {
          continue;
        }
        if (!ianal.direct_inductvars[i1].consti->is_int() ||
            !ianal.direct_inductvars[i2].consti->is_int()) {
          continue;
        }

        auto keep = i2;
        auto repl = i1;
        auto incr_keep = ianal.direct_inductvars[i2].consti->as_int();
        auto incr_repl = ianal.direct_inductvars[i1].consti->as_int();
        if (ianal.direct_inductvars[i1].def.get_type()->get_bitwidth() >
            ianal.direct_inductvars[i2].def.get_type()->get_bitwidth()) {
          keep = i1;
          repl = i2;
          incr_keep = ianal.direct_inductvars[i1].consti->as_int();
          incr_repl = ianal.direct_inductvars[i2].consti->as_int();
        }
        if ((ianal.direct_inductvars[i1].type !=
                 InductionVarAnalysis::IterationType::PlusConst ||
             incr_keep == 0 || incr_repl % incr_keep != 0) &&
            (incr_repl != incr_keep)) {
          continue;
        }

        auto keepes = cfg.bbrs[loop.head].bb->get_arg_id(
            ianal.direct_inductvars[keep].def.as_bb_arg());
        auto repls = cfg.bbrs[loop.head].bb->get_arg_id(
            ianal.direct_inductvars[repl].def.as_bb_arg());
        if (!prehead_bb_term->bbs[prehead_bb_id].args[keepes].is_constant() ||
            !prehead_bb_term->bbs[prehead_bb_id].args[repls].is_constant() ||
            !prehead_bb_term->bbs[prehead_bb_id]
                 .args[keepes]
                 .as_constant()
                 ->is_int() ||
            !prehead_bb_term->bbs[prehead_bb_id]
                 .args[repls]
                 .as_constant()
                 ->is_int()) {
          continue;
        }

        auto starting_valuekeep = prehead_bb_term->bbs[prehead_bb_id]
                                      .args[keepes]
                                      .as_constant()
                                      ->as_int();
        auto starting_valueirepl = prehead_bb_term->bbs[prehead_bb_id]
                                       .args[repls]
                                       .as_constant()
                                       ->as_int();

        if (starting_valuekeep != starting_valueirepl) {
          // TOOD: could also use other ones
          continue;
        }
        fir::Builder b{
            ianal.direct_inductvars[keep].def.as_bb_arg()->get_parent()};
        auto repl_type = ianal.direct_inductvars[repl].def.get_type();
        auto new_repl =
            b.build_itrunc(ianal.direct_inductvars[keep].def, repl_type);
        if (incr_repl != incr_keep) {
          new_repl =
              b.build_int_mul(new_repl, fir::ValueR{ctx->get_constant_value(
                                            incr_repl / incr_keep, repl_type)});
        }
        ianal.direct_inductvars[repl].def.replace_all_uses(new_repl);

        return true;
      }
    }

    return false;
  }

  bool const_exit_value(fir::Context &ctx, CFG &cfg, Dominators &dom,
                        LoopInfoAnalysis &loops, u32 loop_i) {
    // TODO: fix this is kinda dumb
    // return false;
    // if we can calculate the exit value of a a induction variable we can
    // constant "propagate" it into other bbs following this
    (void)dom;
    InductionVarAnalysis ianal{cfg, loops.info[loop_i]};
    if (ianal.direct_inductvars.empty() && ianal.indirect_inductvars.empty()) {
      return false;
    }
    InductionEndValueAnalysis iranal{cfg, loops.info[loop_i], ianal};
    if (iranal.info.empty()) {
      return false;
    }
    bool modified = false;
    for (auto v : iranal.info) {
      auto term = v.from_bb->get_terminator();
      auto out_bbarg_id = term.get_bb_id(v.to_bb);
      auto out_bb_id = cfg.get_bb_id(v.to_bb);

      auto &out_bb_arg = term->bbs[out_bbarg_id];
      for (u32 bb_arg_id = 0; bb_arg_id < out_bb_arg.args.size(); bb_arg_id++) {
        if (v.values.contains(out_bb_arg.args[bb_arg_id])) {
          auto val = ctx->get_constant_int(
              v.values.at(out_bb_arg.args[bb_arg_id]),
              out_bb_arg.args[bb_arg_id].get_type()->get_bitwidth());
          term.replace_bb_arg(out_bbarg_id, bb_arg_id, fir::ValueR{val}, true);
          modified = true;
        }
      }

      TSet<fir::BasicBlock> dominated_blocks;
      for (auto &b : dom.dom_bbs) {
        if (!b.dominators[out_bb_id]) {
          continue;
        }
        dominated_blocks.insert(b.bb);
      }

      for (const auto &[base, known_val] : v.values) {
        auto n_uses = base.get_n_uses();
        for (auto uidp1 = n_uses; uidp1 > 0; uidp1--) {
          auto use = (*base.get_uses())[uidp1 - 1];
          if (dominated_blocks.contains(use.user->get_parent())) {
            use.replace_use(fir::ValueR{
                ctx->get_constant_value(known_val, use.get_type())});
            modified = true;
          }
        }
      }
    }

    if (modified) {
      utils::StatCollector::get().addi(1, "LoopExitValProp",
                                       utils::StatCollector::StatFOptim);
    }
    return modified;
  }

  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("LoopSimplify", COLOR_OPTIMF);
    (void)ctx;
    CFG cfg{func};
    Dominators dom{cfg};
    LoopInfoAnalysis loops{dom};

    size_t loop_ip1 = 1;
    // eliminate all loops that dont have sideffects
    //  TODO: handle if its values are used after and their result value is
    //  known/can be calculated withuot loop
    for (; loop_ip1 <= loops.info.size(); loop_ip1++) {
      if (propagate_to_after_loop(cfg, loops, loop_ip1 - 1) ||
          merge_same_diret_induct_var(cfg, loops, loop_ip1 - 1)) {
        // loop_ip1 = 0;
        // continue;
        // TODO: idk if it invalidates anythign
      }
      if (dead_loop_elimination(cfg, loops, loop_ip1 - 1) ||
          const_exit_value(ctx, cfg, dom, loops, loop_ip1 - 1)) {
        cfg.update(func, false);
        dom.update(cfg);
        loops.update(dom);
        loop_ip1 = 0;
        continue;
      }
    }
  }
};
}  // namespace foptim::optim
