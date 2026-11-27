#include "optim/cost_model/cost.hpp"

#include "optim/cost_model/fir_lowering_estimate.hpp"
#include "utils/todo.hpp"

namespace foptim::optim::cost_model {
Cost cost(fir::Instr instr, CostKind kind, const CostCtx &ctx) {
  ASSERT(ctx.m != nullptr);

  // address arithmetic folded into a memory operand is free for every kind, the
  // AGU computes it with the access
  if (ctx.folds_into_address) {
    return Cost{0.0};
  }

  LoweredOps ops;
  estimate_lowering(instr, ctx, ops);

  double latency = 0.0;
  double rtput = 0.0;
  double size = 0.0;
  for (const auto &op : ops) {
    const TableRow &row = ctx.m->table(op.cls);
    latency += row.latency * op.count;
    rtput += row.recip_throughput * op.count;
    size += row.size_bytes * op.count;
  }

  // a result folded into its user still takes time, so Latency stays, but uops
  // and bytes are in the cost of the user
  if (folded_away_for_resources(ctx) && kind != CostKind::Latency) {
    rtput = 0.0;
    size = 0.0;
  }

  return combine_cost(kind, latency, rtput, size,
                      ctx.m->tune().size_weight);
}

Cost combine_cost(CostKind kind, double latency, double rtput, double size,
                  double size_weight) {
  switch (kind) {
    case CostKind::Throughput:
      return Cost{rtput};
    case CostKind::Latency:
      return Cost{latency};
    case CostKind::Size:
      return Cost{size};
    case CostKind::SizeAndLatency:
      return Cost{latency + size_weight * size};
  }
  UNREACH();
}

}  // namespace foptim::optim::cost_model
