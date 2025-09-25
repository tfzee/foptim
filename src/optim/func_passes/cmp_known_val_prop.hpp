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
    } else if (useEq && cond->is(fir::ICmpInstrSubType::EQ)) {
      if (!cond->args[0].is_constant() && cond->args[1].is_constant() &&
          cfg.bbrs[cfg.get_bb_id(term->bbs[0].bb)].pred.size() == 1) {
        propagate(cond->args[0], cond->args[1], term->get_parent(),
                  term->bbs[0].bb, cfg, dom);
      }
    } else if (useNe && cond->is(fir::ICmpInstrSubType::NE)) {
      if (!cond->args[0].is_constant() && cond->args[1].is_constant() &&
          cfg.bbrs[cfg.get_bb_id(term->bbs[1].bb)].pred.size() == 1) {
        propagate(cond->args[0], cond->args[1], term->get_parent(),
                  term->bbs[1].bb, cfg, dom);
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
        // fmt::println("{:cd}\n{:cd}", cond, term);
        handle_cbranch(cond, term, cfg, dom, true, true);
      }
      // if (term->is(fir::InstrType::SwitchInstr)) {
      //   TODO: handle switch;
      // }
    }
  }
};
}  // namespace foptim::optim
