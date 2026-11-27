#pragma once
#include <span>

#include "ir/basic_block_ref.hpp"
#include "ir/instruction.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/cost_model/machine.hpp"

namespace foptim::optim::cost_model {
// static probability that the edge idx of the conditional branch of bb is
// taken. structural heuristics only: an edge into a block that ends in
// unreachable is cold, an edge back into a dominating block (loop back edge)
// is hot, a null check of a pointer expects non null. everything else is 0.5
[[nodiscard]] double edge_probability(const CFG &cfg, const Dominators &dom,
                                      fir::BasicBlock bb, u32 edge_idx);

// the instructions an arm executes when it is speculated
struct ArmCost {
  // reciprocal throughput cycles of all of them
  double tput = 0.0;
  // critical path through them, values from outside are ready at 0
  double latency = 0.0;
};
[[nodiscard]] ArmCost arm_cost(std::span<const fir::Instr> instrs,
                               const Machine &m);

// fraction of executions of a branch that is taken on the wrong path. the best
// static predictor is wrong min(p, 1 - p) of the time
[[nodiscard]] double mispredict_rate(double p_taken);

struct IfConvertInput {
  // speculated instructions of each arm, an empty arm passes values through
  ArmCost arm0;
  ArmCost arm1;
  // probability of going to arm 0
  double p0 = 0.5;
  // selects that merge the arms, one per value of the join
  u32 n_float_selects = 0;
  u32 n_int_selects = 0;
  // the merged value feeds a loop carried value, so the latency of the arm and
  // of the select lies on the recurrence
  bool carried = false;
};

struct IfConvertCost {
  // cycles per execution
  double branch = 0.0;
  double select = 0.0;
  [[nodiscard]] bool prefer_select() const { return select <= branch; }
};

// branch: the executed arm, the jump and the mispredicts. select: both arms
// and the selects, plus on a carried value the latency of the slower arm and
// the select, where the branch only pays the arm that is taken
[[nodiscard]] IfConvertCost cost_if_convert(const IfConvertInput &in,
                                            const Machine &m);
}  // namespace foptim::optim::cost_model
