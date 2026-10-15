#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include <algorithm>
#include <optional>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
#include "optim/helper/scev_expander.hpp"
#include "utils/set.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {
class LoopSimplify final : public FunctionPass {
public:
  // calls f(value) for every value defined in the loop that is used outside of
  // it (including as an argument of an edge leaving the loop)
  template <class F>
  static void for_each_outside_used(CFG &cfg, const LoopInfo &loop, F &&f) {
    auto outside = [&](const fir::Use &u) {
      if (u.type == fir::UseType::BBArg) {
        return !std::ranges::contains(
            loop.body_nodes, cfg.get_bb_id(u.user->bbs[u.argId].bb));
      }
      return !std::ranges::contains(loop.body_nodes,
                                    cfg.get_bb_id(u.user->get_parent()));
    };
    for (auto b : loop.body_nodes) {
      for (auto arg : cfg.bbrs[b].bb->args) {
        if (std::ranges::any_of(arg->uses, outside)) {
          f(fir::ValueR{arg});
        }
      }
      for (auto i : cfg.bbrs[b].bb->instructions) {
        if (std::ranges::any_of(i->uses, outside)) {
          f(fir::ValueR{i});
        }
      }
    }
  }

  static bool has_sideeffects(CFG &cfg, const LoopInfo &loop) {
    for (auto b : loop.body_nodes) {
      for (auto i : cfg.bbrs[b].bb->instructions) {
        if (i->has_pot_sideeffects()) {
          return true;
        }
      }
    }
    return false;
  }

  bool dead_loop_elimination(CFG &cfg, LoopInfoAnalysis &loops, u32 loop_i) {
    auto &loop = loops.info.at(loop_i);
    if (has_sideeffects(cfg, loop)) {
      return false;
    }
    bool used_outside = false;
    for_each_outside_used(cfg, loop, [&](fir::ValueR) { used_outside = true; });
    if (used_outside) {
      return false;
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
    bool modified = false;
    // expressions of loaded values stay in the loop, copying them behind it
    // keeps the loads alive there and hides reductions from the vectorizer
    TMap<fir::Instr, bool> loaded;
    auto depends_on_load = [&](auto &self, fir::Instr i) -> bool {
      if (auto it = loaded.find(i); it != loaded.end()) {
        return it->second;
      }
      loaded.insert({i, false});
      bool res = i->pot_reads_mem();
      for (auto a : i->args) {
        if (!res && a.is_instr() &&
            body_nodes_lut.contains(a.as_instr()->get_parent())) {
          res = self(self, a.as_instr());
        }
      }
      loaded[i] = res;
      return res;
    };
    for (auto b_id : loop.body_nodes) {
      for (auto instr : cfg.bbrs[b_id].bb->instructions) {
        // TODO(PERF): could move loads but only if theres no interfering writes
        // before this instruction and any potential exit
        if (instr->has_pot_sideeffects() || instr->pot_reads_mem() ||
            instr->pot_modifies_mem() || instr->is_critical() ||
            depends_on_load(depends_on_load, instr)) {
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
          modified = true;
          utils::StatCollector::get().addi(1, "PropagateUseAfter",
                                           utils::StatCollector::StatFOptim);
        }
      }
    }
    return modified;
  }

  bool merge_same_diret_induct_var(CFG &cfg, LoopInfoAnalysis &loops,
                                   u32 loop_i) {
    // two header arguments {s1,+,c1} (kept) and {s2,+,c2} with c2 = r * c1 and
    // s2 = r * s1 are the same induction variable, the second one can be
    // computed from the first (truncated to the narrower type if needed)
    auto &loop = loops.info[loop_i];
    if (cfg.bbrs[loop.head].pred.size() != loop.tails.size() + 1) {
      return false;
    }

    auto *ctx = cfg.func->ctx;
    ScalarEvo evo{cfg, loop};
    auto head_bb = cfg.bbrs[loop.head].bb;
    auto args = head_bb->args;
    for (size_t i1 = 0; i1 < args.size(); i1++) {
      for (size_t i2 = i1 + 1; i2 < args.size(); i2++) {
        const fir::ValueR v1{args[i1]};
        const fir::ValueR v2{args[i2]};
        if (!v1.get_type()->is_int() || !v2.get_type()->is_int()) {
          continue;
        }
        const SCEVRef r1 = evo.get(v1);
        const SCEVRef r2 = evo.get(v2);
        if (!evo.is_const_addrec(r1) || !evo.is_const_addrec(r2)) {
          continue;
        }

        // keep the wider one
        auto keep = v2;
        auto repl = v1;
        auto rkeep = r2;
        auto rrepl = r1;
        if (v1.get_type()->get_bitwidth() > v2.get_type()->get_bitwidth()) {
          keep = v1;
          repl = v2;
          rkeep = r1;
          rrepl = r2;
        }
        const i128 step_keep = evo.expr(evo.expr(rkeep).b).cval;
        const i128 step_repl = evo.expr(evo.expr(rrepl).b).cval;
        const i128 start_keep = evo.expr(evo.expr(rkeep).a).cval;
        const i128 start_repl = evo.expr(evo.expr(rrepl).a).cval;
        const u32 repl_bw = repl.get_type()->get_bitwidth();
        if (step_keep == 0 || step_repl % step_keep != 0) {
          continue;
        }
        const i128 ratio = step_repl / step_keep;
        // trunc(keep) * ratio has to start where repl starts
        const i128 expected_start = static_cast<i128>(
            static_cast<u128>(start_keep) * static_cast<u128>(ratio));
        const i128 mask = (static_cast<i128>(1) << repl_bw) - 1;
        if (((expected_start - start_repl) & mask) != 0) {
          continue;
        }

        fir::Builder b{head_bb};
        fir::ValueR new_repl = keep;
        if (keep.get_type()->get_bitwidth() != repl_bw) {
          new_repl = b.build_itrunc(keep, repl.get_type());
        }
        if (ratio != 1) {
          new_repl = b.build_int_mul(
              new_repl,
              fir::ValueR{ctx->get_constant_value(ratio, repl.get_type())});
        }
        repl.replace_all_uses(new_repl);

        return true;
      }
    }

    return false;
  }

  bool const_exit_value(fir::Context &ctx, CFG &cfg, Dominators &dom,
                        LoopInfoAnalysis &loops, u32 loop_i) {
    // if we can calculate the exit value of an induction variable (a constant
    // or a closed form of loop invariants) we can "propagate" it into the
    // blocks following the loop
    auto &loop = loops.info[loop_i];
    ScalarEvo evo{cfg, loop};
    InductionEndValueAnalysis iranal{cfg, dom, loop, evo};
    if (iranal.info.empty()) {
      return false;
    }
    bool modified = false;
    for (const auto &v : iranal.info) {
      // Closed forms that are not constants cost instructions in front of the
      // loop, they are only worth it if nothing is left that keeps the loop
      // alive: no side effects and every value used after the loop has one.
      bool removable = !has_sideeffects(cfg, loop);
      for_each_outside_used(cfg, loop, [&](fir::ValueR used) {
        removable = removable && v.values.contains(used);
      });
      // closed forms are computed in the block that enters the loop
      std::optional<SCEVExpander> expander;
      TMap<fir::ValueR, fir::ValueR> end_values;
      auto end_value = [&](fir::ValueR base) -> std::optional<fir::ValueR> {
        if (auto it = end_values.find(base); it != end_values.end()) {
          return it->second;
        }
        const SCEVRef r = v.values.at(base);
        fir::ValueR res;
        if (!evo.is_const(r) && !removable) {
          return std::nullopt;
        } else if (evo.is_const(r)) {
          res = fir::ValueR{ctx->get_constant_int(
              evo.expr(r).cval, base.get_type()->get_bitwidth())};
        } else {
          if (!evo.entry_block().is_valid()) {
            return std::nullopt;
          }
          if (!expander) {
            expander.emplace(ctx, evo, evo.entry_block()->get_terminator());
          }
          res = expander->expand(r);
        }
        end_values.insert({base, res});
        return res;
      };

      auto term = v.from_bb->get_terminator();
      auto out_bbarg_id = term.get_bb_id(v.to_bb);
      auto out_bb_id = cfg.get_bb_id(v.to_bb);

      auto &out_bb_arg = term->bbs[out_bbarg_id];
      for (u32 bb_arg_id = 0; bb_arg_id < out_bb_arg.args.size(); bb_arg_id++) {
        if (v.values.contains(out_bb_arg.args[bb_arg_id])) {
          const auto val = end_value(out_bb_arg.args[bb_arg_id]);
          if (!val) {
            continue;
          }
          term.replace_bb_arg(out_bbarg_id, bb_arg_id, *val, true);
          modified = true;
        }
      }

      TSet<fir::BasicBlock> dominated_blocks;
      for (u32 b_id = 0; b_id < cfg.bbrs.size(); b_id++) {
        if (!dom.dominates(out_bb_id, b_id)) {
          continue;
        }
        dominated_blocks.insert(cfg.bbrs[b_id].bb);
      }

      for (const auto &[base, known_val] : v.values) {
        auto n_uses = base.get_n_uses();
        for (auto uidp1 = n_uses; uidp1 > 0; uidp1--) {
          auto use = (*base.get_uses())[uidp1 - 1];
          // the exit block can dominate the loop itself (nested loops), the
          // uses inside the loop see the per iteration values
          if (std::ranges::contains(
                  loop.body_nodes,
                  cfg.get_bb_id(use.user->get_parent()))) {
            continue;
          }
          if (dominated_blocks.contains(use.user->get_parent())) {
            const auto val = end_value(base);
            if (!val) {
              continue;
            }
            use.replace_use(*val);
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

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("LoopSimplify", COLOR_OPTIMF);
    (void)ctx;
    CFG &cfg = AnalysisManager::cfg(func);
    Dominators &dom = AnalysisManager::dom(func);
    LoopInfoAnalysis loops{dom};

    bool modified = false;
    size_t loop_ip1 = 1;
    // eliminate all loops that dont have sideffects
    //  TODO: handle if its values are used after and their result value is
    //  known/can be calculated withuot loop
    for (; loop_ip1 <= loops.info.size(); loop_ip1++) {
      if (propagate_to_after_loop(cfg, loops, loop_ip1 - 1) ||
          merge_same_diret_induct_var(cfg, loops, loop_ip1 - 1)) {
        modified = true;
        // loop_ip1 = 0;
        // continue;
      }
      if (dead_loop_elimination(cfg, loops, loop_ip1 - 1) ||
          const_exit_value(ctx, cfg, dom, loops, loop_ip1 - 1)) {
        modified = true;
        cfg.update(func, false);
        dom.update(cfg);
        loops.update(dom);
        loop_ip1 = 0;
        continue;
      }
    }
    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};
} // namespace foptim::optim
