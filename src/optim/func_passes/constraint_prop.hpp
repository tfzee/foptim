#pragma once
#include "../function_pass.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/constraint_analysis.hpp"

namespace foptim::optim {

// Propagate known constraints. The analysis is built once and all
// queries are answered before anything is replaced, removing the need to
// recalculate expensive analysis.
class ConstraintProp final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("ConstraintProp", COLOR_OPTIMF);
    bool has_branch = false;
    for (const auto bb : func.get_bbs()) {
      const auto term = bb->get_terminator();
      if (term->is(fir::InstrType::CondBranchInstr) ||
          term->is(fir::InstrType::SwitchInstr)) {
        has_branch = true;
        break;
      }
    }
    if (!has_branch) {
      return PreservedAnalysis::all();
    }

    CFG &cfg = AnalysisManager::cfg(func);
    ConstraintAnalysis &constr = AnalysisManager::constraints(func);
    const auto bool_ty = ctx->get_int_type(1);

    TVec<fir::Instr> always_false;
    TVec<fir::Instr> always_true;
    constr.walk_with_facts([&](u32 bb_id, std::span<const u32> facts) {
      if (facts.empty()) {
        return;
      }
      for (auto instr : cfg.bbrs[bb_id].bb->get_instrs()) {
        if (instr->instr_type != fir::InstrType::ICmp ||
            instr.result_type() != bool_ty) {
          continue;
        }
        const auto cond = constr.get_constraint(instr);
        if (cond.has_value() && constr.contradicts(cond.value(), facts)) {
          always_false.push_back(instr);
        } else if (cond.has_value() && constr.implied(cond.value(), facts)) {
          always_true.push_back(instr);
        }
      }
    });

    for (auto instr : always_false) {
      instr->replace_all_uses(fir::ValueR{ctx->get_constant_int(0, 1)});
    }
    for (auto instr : always_true) {
      instr->replace_all_uses(fir::ValueR{ctx->get_constant_int(1, 1)});
    }
    // only uses get replaced by constants
    return always_false.empty() && always_true.empty() ? PreservedAnalysis::all()
                                : PreservedAnalysis::cfg_only();
  }
};
}  // namespace foptim::optim
