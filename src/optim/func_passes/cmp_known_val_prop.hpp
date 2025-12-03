#pragma once
#include <fmt/core.h>

#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"

namespace foptim::optim {
class CmpKnownValProp final : public FunctionPass {
  void propagate(fir::ValueR input, fir::ValueR val, fir::BasicBlock /*condbb*/,
                 fir::BasicBlock targetbb, CFG& cfg, Dominators& dom) {
    auto targetbb_id = cfg.get_bb_id(targetbb);
    if (const auto* old_use_ref = input.get_uses()) {
      TVec old_uses = *old_use_ref;
      for (auto u : old_uses) {
        auto use_bb = u.user->get_parent();
        if (dom.dom_bbs[cfg.get_bb_id(use_bb)].dominators[targetbb_id]) {
          u.replace_use(val);
        }
      }
    }
  }

  void handle_cbranch(fir::Instr cond, fir::Instr term, CFG& cfg,
                      Dominators& dom, bool useEq, bool useNe) {
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

  void propagate_known_cond(fir::Instr term, CFG& cfg) {
    // only if we have multiple uses
    if (term->args[0].get_n_uses() < 2) {
      return;
    }
    utils::BitSet reachable_bbs_true(cfg.bbrs.size(), false);
    utils::BitSet reachable_bbs_false(cfg.bbrs.size(), false);
    {
      // collect all the reachable blocks
      auto next_bb_true = cfg.get_bb_id(term->bbs[0].bb);
      reachable_bbs_true[next_bb_true].set(true);
      TVec<u32> worklist;
      worklist.push_back(next_bb_true);
      while (!worklist.empty()) {
        u32 c = worklist.back();
        worklist.pop_back();
        for (auto n : cfg.bbrs[c].succ) {
          if (!reachable_bbs_true[n]) {
            reachable_bbs_true[n].set(true);
            worklist.push_back(n);
          }
        }
      }

      auto next_bb_false = cfg.get_bb_id(term->bbs[1].bb);
      reachable_bbs_false[next_bb_false].set(true);
      worklist.push_back(next_bb_false);
      while (!worklist.empty()) {
        u32 c = worklist.back();
        worklist.pop_back();
        for (auto n : cfg.bbrs[c].succ) {
          if (!reachable_bbs_false[n]) {
            reachable_bbs_false[n].set(true);
            worklist.push_back(n);
          }
        }
      }
    }
    // we only care about blocks that can only be reached by one of the two
    // paths
    auto overlap = reachable_bbs_true;
    overlap.mul(reachable_bbs_false);
    reachable_bbs_true.mul_not(overlap);
    reachable_bbs_false.mul_not(overlap);
    TVec<fir::Use> uses_copy{term->args[0].get_uses()->begin(),
                             term->args[0].get_uses()->end()};
    for (auto use : uses_copy) {
      auto user_bb = cfg.get_bb_id(use.user->get_parent());
      if (reachable_bbs_true[user_bb]) {
        use.replace_use(fir::ValueR{cfg.func->ctx->get_constant_int(1, 1)});
      }
      if (reachable_bbs_false[user_bb]) {
        use.replace_use(fir::ValueR{cfg.func->ctx->get_constant_int(0, 1)});
      }
    }
  }

 public:
  void apply(fir::Context& /*ctx*/, fir::Function& func) override {
    ZoneScopedNC("CmpKnownValProp", COLOR_OPTIMF);
    CFG cfg{func};
    Dominators dom{cfg};

    for (auto bb : func.basic_blocks) {
      auto term = bb->get_terminator();
      if (term->is(fir::InstrType::CondBranchInstr) &&
          term->args[0].is_instr() && term->bbs[0].bb != term->bbs[1].bb) {
        auto cond = term->args[0].as_instr();
        handle_cbranch(cond, term, cfg, dom, true, true);
        propagate_known_cond(term, cfg);
      }
    }
  }
};
}  // namespace foptim::optim
