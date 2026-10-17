#pragma once
#include <optional>
#include <type_traits>

#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/constraint_analysis.hpp"

namespace foptim::optim {

constexpr bool TRACY_DEBUG_INST_SIMPLIFY = false;

class InstSimplify final : public FunctionPass {
public:
  struct WorkItem {
    fir::Instr instr;
    fir::BasicBlock b;
  };

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

namespace InstSimp {

// Lazily built ConstraintAnalysis shared by all icmps of one InstSimplify
// run. Any change to the IR drops it (invalidate()), the next query rebuilds
// it. The CFG comes from the AnalysisManager as long as no terminator was
// changed by this pass, afterwards a private copy is used.
class ConstraintCache {
  fir::Function *func;
  std::optional<CFG> local_cfg;
  std::optional<ConstraintAnalysis> constr;
  bool cfg_stale = false;
  // does the function have any conditional branch at all (otherwise no
  // constraints exist and building the analysis is pointless)
  std::optional<bool> has_cond_branch;

  CFG &cfg() {
    if (!cfg_stale) {
      return AnalysisManager::cfg(*func);
    }
    if (!local_cfg.has_value()) {
      local_cfg.emplace(*func);
    }
    return *local_cfg;
  }

public:
  explicit ConstraintCache(fir::Function &func) : func(&func) {}

  // some instruction changed
  void invalidate(bool terminator_changed) {
    constr.reset();
    local_cfg.reset();
    cfg_stale |= terminator_changed;
    if (terminator_changed) {
      has_cond_branch.reset();
    }
  }

  // nullptr if the function cant produce any constraints
  ConstraintAnalysis *get() {
    if (!has_cond_branch.has_value()) {
      bool found = false;
      for (const auto bb : func->get_bbs()) {
        if (bb->get_terminator()->is(fir::InstrType::CondBranchInstr)) {
          found = true;
          break;
        }
      }
      has_cond_branch = found;
    }
    if (!*has_cond_branch) {
      return nullptr;
    }
    if (!constr.has_value()) {
      constr.emplace(cfg());
    }
    return &*constr;
  }
  CFG &get_cfg() { return cfg(); }
};

using WorkList = TVec<InstSimplify::WorkItem>;

inline void push_all_uses(WorkList &worklist, fir::Instr instr) {
  for (auto &use : instr->uses) {
    worklist.emplace_back(use.user, use.user->parent);
  }
}

} // namespace InstSimp
} // namespace foptim::optim
