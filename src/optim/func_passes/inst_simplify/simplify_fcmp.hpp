#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "optim/func_passes/inst_simplify.hpp"

namespace foptim::optim::InstSimp {
// f32 constants keep their bit pattern in the f64 slot
f64 const_fp(fir::ConstantValueR c);

bool simplify_fcmp(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                   WorkList &worklist);

}  // namespace foptim::optim::InstSimp
