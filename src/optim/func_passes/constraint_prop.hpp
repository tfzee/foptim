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
    bool has_cond_branch = false;
    for (const auto bb : func.get_bbs()) {
      if (bb->get_terminator()->is(fir::InstrType::CondBranchInstr)) {
        has_cond_branch = true;
        break;
      }
    }
    if (!has_cond_branch) {
      return PreservedAnalysis::all();
    }

    CFG &cfg = AnalysisManager::cfg(func);
    ConstraintAnalysis &constr = AnalysisManager::constraints(func);
    const auto bool_ty = ctx->get_int_type(1);

    TVec<fir::Instr> always_false;
    for (const auto bb : func.get_bbs()) {
      auto &facts =
          constr.bb_to_constraints[cfg.get_bb_id(bb)].active_constraints;
      if (facts.empty()) {
        continue;
      }
      for (auto instr : bb->get_instrs()) {
        if (instr->instr_type != fir::InstrType::ICmp ||
            instr.result_type() != bool_ty) {
          continue;
        }
        const auto cond = constr.get_constraint(instr);
        if (cond.has_value() && constr.contradicts(cond.value(), facts)) {
          always_false.push_back(instr);
        }
      }
    }

    for (auto instr : always_false) {
      instr->replace_all_uses(fir::ValueR{ctx->get_constant_int(0, 1)});
    }
    // only uses get replaced by constants
    return always_false.empty() ? PreservedAnalysis::all()
                                : PreservedAnalysis::cfg_only();
  }
};
}  // namespace foptim::optim
