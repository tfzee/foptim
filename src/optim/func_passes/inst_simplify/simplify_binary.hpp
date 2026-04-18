#pragma once
#include <fmt/base.h>

#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/func_passes/inst_simplify.hpp"

namespace foptim::optim::InstSimp {
bool simplify_binary(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                     WorkList &worklist, AttributerManager &man);

}  // namespace foptim::optim::InstSimp
