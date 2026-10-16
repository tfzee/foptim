#pragma once
#include <fmt/core.h>

#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"

namespace foptim::optim {
class CmpKnownValProp final : public FunctionPass {
  // whether any use was replaced
  bool modified = false;

  void propagate(fir::ValueR input, fir::ValueR val, fir::BasicBlock /*condbb*/,
                 fir::BasicBlock targetbb, CFG &cfg, Dominators &dom) {
    auto targetbb_id = cfg.get_bb_id(targetbb);
    if (const auto *old_use_ref = input.get_uses()) {
      TVec old_uses = *old_use_ref;
      for (auto u : old_uses) {
        auto use_bb = u.user->get_parent();
        if (dom.strict_dominates(targetbb_id, cfg.get_bb_id(use_bb))) {
          u.replace_use(val);
          modified = true;
        }
      }
    }
  }

  void handle_cbranch(fir::Instr cond, fir::Instr term, CFG &cfg,
                      Dominators &dom, bool useEq, bool useNe) {
    if (cond->is(fir::BinaryInstrSubType::And)) {
      if (cond->args[0].is_instr()) {
        handle_cbranch(cond->args[0].as_instr(), term, cfg, dom, true, false);
      }
      if (cond->args[1].is_instr()) {
        handle_cbranch(cond->args[1].as_instr(), term, cfg, dom, true, false);
      }
    } else if (cond->is(fir::BinaryInstrSubType::Or)) {
      if (cond->args[0].is_instr()) {
        handle_cbranch(cond->args[0].as_instr(), term, cfg, dom, false, true);
      }
      if (cond->args[1].is_instr()) {
        handle_cbranch(cond->args[1].as_instr(), term, cfg, dom, false, true);
      }
    } else if (useEq && (cond->is(fir::ICmpInstrSubType::EQ) ||
                         cond->is(fir::FCmpInstrSubType::OEQ))) {
      if (!cond->args[0].is_constant() && cond->args[1].is_constant() &&
          cfg.bbrs[cfg.get_bb_id(term->bbs[0].bb)].pred.size() == 1) {
        propagate(cond->args[0], cond->args[1], term->get_parent(),
                  term->bbs[0].bb, cfg, dom);
      }
    } else if (useNe && (cond->is(fir::ICmpInstrSubType::NE) ||
                         cond->is(fir::FCmpInstrSubType::ONE))) {
      if (!cond->args[0].is_constant() && cond->args[1].is_constant() &&
          cfg.bbrs[cfg.get_bb_id(term->bbs[1].bb)].pred.size() == 1) {
        propagate(cond->args[0], cond->args[1], term->get_parent(),
                  term->bbs[1].bb, cfg, dom);
      }
    }
  }

  void propagate_known_cond(fir::Instr term, CFG &cfg, Dominators &dom) {
    // only if we have multiple uses
    if (term->args[0].get_n_uses() < 2) {
      return;
    }
    // a use is only known if every execution of it came through the edge, so
    // the target has to be entered only by this edge and dominate the use.
    // Blocks that merely can be reached from the target are not enough, the
    // value may be recomputed on the way (loops through its definition).
    const auto replace_dominated = [&](fir::BasicBlock target, i64 val) {
      auto target_id = cfg.get_bb_id(target);
      if (cfg.bbrs[target_id].pred.size() != 1) {
        return;
      }
      TVec<fir::Use> uses_copy{term->args[0].get_uses()->begin(),
                               term->args[0].get_uses()->end()};
      for (auto use : uses_copy) {
        auto user_bb = cfg.get_bb_id(use.user->get_parent());
        if (dom.dominates(target_id, user_bb)) {
          use.replace_use(fir::ValueR{cfg.func->ctx->get_constant_int(val, 1)});
          modified = true;
        }
      }
    };
    replace_dominated(term->bbs[0].bb, 1);
    replace_dominated(term->bbs[1].bb, 0);
  }

 public:
  PreservedAnalysis apply(fir::Context & /*ctx*/,
                          fir::Function &func) override {
    ZoneScopedNC("CmpKnownValProp", COLOR_OPTIMF);
    CFG &cfg = AnalysisManager::cfg(func);
    Dominators &dom = AnalysisManager::dom(func);

    for (auto bb : func.basic_blocks) {
      auto term = bb->get_terminator();
      if (term->is(fir::InstrType::CondBranchInstr) &&
          term->args[0].is_instr() && term->bbs[0].bb != term->bbs[1].bb) {
        auto cond = term->args[0].as_instr();
        handle_cbranch(cond, term, cfg, dom, true, true);
        propagate_known_cond(term, cfg, dom);
      }
    }
    // only uses get replaced by constants
    return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
  }
};
}  // namespace foptim::optim
