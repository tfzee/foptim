#pragma once
#include "ir/instruction.hpp"
#include "optim/cost_model/cost.hpp"
#include "optim/cost_model/x86_op_class.hpp"

namespace foptim::optim::cost_model {
struct LoweredOp {
  X86OpClass cls;
  // repeat count, for example a 512 bit op split on an AVX2 target
  double count = 1.0;
};

// a handful of ops per FIR instruction, fixed array
struct LoweredOps {
  static constexpr u8 kMax = 4;
  LoweredOp ops[kMax] = {};
  u8 n = 0;

  void push_back(LoweredOp op) {
    ASSERT(n < kMax);
    ops[n++] = op;
  }
  [[nodiscard]] u8 size() const { return n; }
  [[nodiscard]] const LoweredOp *begin() const { return ops; }
  [[nodiscard]] const LoweredOp *end() const { return ops + n; }
};

// shadow instruction selection: which short sequence of x86 shape classes instr
// lowers to given ctx. mirrors the shape predicates of the matcher without
// sharing code. precise for address folding, load folding into ALU ops, cmp+jcc
// fusion, free 32 to 64 zext and division, popcnt and lzcnt through the Machine
// table, everything else a flat fallback
void estimate_lowering(fir::Instr instr, const CostCtx &ctx, LoweredOps &out);

// fills ctx.folds_into_address and folds_into_user from how the single result
// is used, the same shapes as the matcher address mode and cmp+jcc rules,
// evaluated on FIR def use. callers walking a block call it per instruction
// before costing, tests may set the fields by hand
void detect_fold_context(fir::Instr instr, CostCtx &ctx);

}  // namespace foptim::optim::cost_model
