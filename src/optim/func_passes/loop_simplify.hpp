#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {

class LoopSimplify final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    (void)ctx;
    CFG cfg{func};
    Dominators dom{cfg};
    LoopInfoAnalysis loops{dom};

    for (auto loop : loops.info) {
      InductionVarAnalysis induct{cfg, loop};

      bool failed = false;
      fir::Instr v{fir::Instr::invalid()};

      for (auto leaving : loop.leaving_nodes) {
        auto term = cfg.bbrs[leaving].bb->get_terminator();
        ASSERT(term->is(fir::InstrType::CondBranchInstr));
        ASSERT(term->args[0].is_instr());
        auto cond = term->args[0].as_instr();
        if (!cond->is(fir::InstrType::ICmp)) {
          failed = true;
          break;
        }
        if (!cond->args[0].is_instr() || !cond->args[1].is_constant()) {
          failed = true;
          break;
        }
        if (!v.is_valid()) {
          v = cond;
        } else if (v != cond) {
          failed = true;
          break;
        }
      }
      if (failed) {
        continue;
      }
      fmt::println("{}", func);
      auto cond_cond = (fir::ICmpInstrSubType)v->subtype;
      auto cond_input = v->args[0].as_instr();
      auto cond_bound = v->args[1].as_constant();
      bool incrementing;
      bool found = false;
      for (auto &var : induct.direct_inductvars) {
        incrementing =
            var.type == InductionVarAnalysis::IterationType::PlusConst;
        if (var.def == fir::ValueR{cond_input} && var.consti->is_int() &&
            (incrementing ||
             var.type == InductionVarAnalysis::IterationType::SubConst)) {
          auto v = var.consti->as_int();
          if (v == 1) {
            continue;
          } else if (v == -1) {
            incrementing = !incrementing;
          } else {
            break;
          }
          found = true;
          break;
        }
      }
      for (auto &var : induct.indirect_inductvars) {
        incrementing =
            var.type == InductionVarAnalysis::IterationType::PlusConst;
        if (var.def == fir::ValueR{cond_input} &&
            (incrementing ||
             var.type == InductionVarAnalysis::IterationType::SubConst)) {
          auto v = var.arg2.as_constant()->as_int();
          if (v == 1) {
            continue;
          } else if (v == -1) {
            incrementing = !incrementing;
          } else {
            break;
          }
          found = true;
          break;
        }
      }
      if (!found || failed) {
        continue;
      }

      (void)cond_cond;
      fmt::println("{}", v);
      fmt::println("{}", cond_input);
      fmt::println("{}", cond_bound);
      fmt::println("{}", incrementing);
      fmt::println("{}", v);

      induct.dump();
    }
  }
};
} // namespace foptim::optim
