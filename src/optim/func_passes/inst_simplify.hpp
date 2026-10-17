#pragma once
#include <type_traits>

#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"

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

using WorkList = TVec<InstSimplify::WorkItem>;

inline void push_all_uses(WorkList &worklist, fir::Instr instr) {
  for (auto &use : instr->uses) {
    worklist.emplace_back(use.user, use.user->parent);
  }
}

} // namespace InstSimp
} // namespace foptim::optim
