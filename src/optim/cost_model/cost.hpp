#pragma once
#include <optional>

#include "ir/instruction.hpp"
#include "optim/cost_model/machine.hpp"

namespace foptim::optim::cost_model {
enum class CostKind {
  Throughput,
  Latency,
  Size,
  // combined code growth and speed score for callers that want one number for example
  // da inliner
  SizeAndLatency,
};

// context that changes what an instruction costs independent of its opcode.
// duplicates a minimal subset of the matcher shape predicates
// (fir_lowering_estimate.cpp) instead of sharing code idk if bad idea but this keeps it cheaper but need to sync maybe
struct CostCtx {
  const Machine *m = nullptr;
  // lane count when vectorized, unset for scalars
  std::optional<u32> vf;
  // which of up to 3 operands are constants
  bool operand_is_const[3] = {false, false, false};
  // address arithmetic that folds into the memory operand of its single user,
  // free
  bool folds_into_address = false;
  // result consumed directly by its single user as a memory operand or fused
  // compare, charged to the user
  bool folds_into_user = false;
};

// cost in the unit of kind, different kinds are not on a shared scale,
// SizeAndLatency exists for that
struct Cost {
  double value = 0.0;
};

// true when ctx contributes no uop or bytes of its own (folded). cost() already
// zeroes those, callers that total resources over estimate_lowering rows must
// skip such instructions too
inline bool folded_away_for_resources(const CostCtx &ctx) {
  return ctx.folds_into_address || ctx.folds_into_user;
}

// picks the number of kind out of the three totals of a lowering
// size_weight is TuneParams::size_weight, it only matters for SizeAndLatency
Cost combine_cost(CostKind kind, double latency, double rtput, double size,
                  double size_weight);

// cost of instr under kind in the context ctx
Cost cost(fir::Instr instr, CostKind kind, const CostCtx &ctx);

}  // namespace foptim::optim::cost_model
