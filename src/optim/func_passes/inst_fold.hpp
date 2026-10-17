#pragma once
#include <optional>

#include "../function_pass.hpp"
#include "ir/instruction.hpp"
#include "ir/value.hpp"
#include "optim/analysis/attributer/attributer.hpp"

namespace foptim::optim {

namespace InstFold {

// The value that replaces the instruction, if it folds. The result is a
// constant (new or existing) or a value that already exists, nothing is
// created or edited. Terminators and instructions with side effects (stores,
// calls, volatile accesses) never fold. The KnownBits analyses used for
// compares may set nsw / nuw on adds in the manager, check
// AttributerManager::ir_modified afterwards. Effectively copying llvm design to
// split this from inst simplify
std::optional<fir::ValueR> fold(fir::Instr instr, fir::Context &ctx,
                                AttributerManager &man);

}  // namespace InstFold

// Replaces every instruction that InstFold::fold can evaluate, users of a
// folded instruction are looked at again.
class InstFoldPass final : public FunctionPass {
 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
