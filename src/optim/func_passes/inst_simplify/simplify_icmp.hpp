#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/func_passes/inst_simplify.hpp"

namespace foptim::optim::InstSimp {
bool simplify_icmp(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                   WorkList &worklist, AttributerManager &man,
                   ConstraintCache &ccache);

} // namespace foptim::optim::InstSimp
