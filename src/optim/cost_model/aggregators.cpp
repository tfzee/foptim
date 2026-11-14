#include "optim/cost_model/aggregators.hpp"

#include <algorithm>
#include <bit>

#include "ir/basic_block.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/function.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/cost_model/cost.hpp"
#include "optim/cost_model/fir_lowering_estimate.hpp"
#include "utils/map.hpp"
#include "utils/set.hpp"

namespace foptim::optim::cost_model {
BlockThroughputCost::BlockThroughputCost(fir::BasicBlock bb, const Machine &m) {
  TMap<fir::ValueR, double> finish_time;
  double port_uops[kNumPorts] = {};

  for (auto instr : bb->instructions) {
    CostCtx ctx;
    ctx.m = &m;
    detect_fold_context(instr, ctx);

    double ready = 0.0;
    for (const auto &arg : instr->args) {
      if (arg.is_instr()) {
        if (auto it = finish_time.find(arg); it != finish_time.end()) {
          ready = std::max(ready, it->second);
        }
      }
    }
    const double lat = cost(instr, CostKind::Latency, ctx).value;
    const double finish = ready + lat;
    if (instr->has_one_result()) {
      finish_time[instr.result()] = finish;
    }
    critical_path_ = std::max(critical_path_, finish);

    if (!folded_away_for_resources(ctx)) {
      LoweredOps ops;
      estimate_lowering(instr, ctx, ops);
      for (const auto &op : ops) {
        const TableRow &row = m.table(op.cls);
        const u32 n_elig =
            std::popcount(static_cast<unsigned>(row.ports_mask));
        if (n_elig == 0) {
          continue;
        }
        // uops on several ports are spread evenly
        const double share = op.count * row.n_uops / static_cast<double>(n_elig);
        for (u32 p = 0; p < m.n_ports; p++) {
          if ((row.ports_mask >> p) & 1u) {
            port_uops[p] += share;
          }
        }
      }
    }
  }

  resource_bound_ = *std::max_element(port_uops, port_uops + m.n_ports);
}

namespace {
// walks from the back edge value of one header argument along first operand
// chains adding latency until it closes on the argument (i = i + step, acc =
// acc + x) or leaves the body or hits the guard limit. not a general critical
// path solver
double chain_latency_from(fir::ValueR back_val, fir::BBArgument header_arg,
                          CFG &cfg, LoopInfo &loop, const Machine &m) {
  double chain = 0.0;
  fir::ValueR cur = back_val;
  for (int guard = 0; guard < 64 && cur.is_instr(); guard++) {
    const fir::Instr cur_instr = cur.as_instr();
    const fir::BasicBlock parent = cur_instr->get_parent();
    if (!loop.contains(cfg.get_bb_id(parent))) {
      break;
    }
    CostCtx ctx;
    ctx.m = &m;
    detect_fold_context(cur_instr, ctx);
    chain += cost(cur_instr, CostKind::Latency, ctx).value;

    if (cur_instr->args.empty()) {
      break;
    }
    bool closes = false;
    for (const auto &op : cur_instr->args) {
      if (op.is_bb_arg() && op.as_bb_arg() == header_arg) {
        closes = true;
        break;
      }
    }
    if (closes) {
      break;
    }
    cur = cur_instr->args[0];
  }
  return chain;
}

double compute_chain_latency(CFG &cfg, LoopInfo &loop, const Machine &m) {
  const fir::BasicBlock header = cfg.bbrs[loop.head].bb;
  double max_chain = 0.0;
  for (const u32 tail_id : loop.tails) {
    const fir::BasicBlock tail = cfg.bbrs[tail_id].bb;
    const fir::Instr term = tail->get_terminator();
    const u16 bb_id = term.get_bb_id(header);
    const auto &incoming = term->bbs[bb_id].args;
    for (size_t a = 0; a < header->args.size() && a < incoming.size(); a++) {
      max_chain = std::max(
          max_chain,
          chain_latency_from(incoming[a], header->args[a], cfg, loop, m));
    }
  }
  return max_chain;
}

}  // namespace

LoopCostAnalysis::LoopCostAnalysis(CFG &cfg, Dominators &dom, LoopInfo &loop,
                                    ScalarEvo &evo, const Machine &m,
                                    double assumed_trip_count)
    : assumed_trip_count_(assumed_trip_count) {
  for (const u32 bb_id : loop.body_nodes) {
    iter_resource_bound_ +=
        BlockThroughputCost(cfg.bbrs[bb_id].bb, m).estimate();
  }
  chain_latency_ = compute_chain_latency(cfg, loop, m);

  TripCountAnalysis tc;
  if (tc.update(cfg, dom, loop, evo) && tc.const_exit_iter.has_value()) {
    // the body runs exit_iter + 1 times
    trip_count_ = static_cast<double>(*tc.const_exit_iter) + 1.0;
  } else {
    trip_count_ = std::nullopt;
  }
}

double size_cost(fir::BasicBlock bb, const Machine &m) {
  double total = 0.0;
  for (auto instr : bb->instructions) {
    CostCtx ctx;
    ctx.m = &m;
    detect_fold_context(instr, ctx);
    total += cost(instr, CostKind::Size, ctx).value;
  }
  return total;
}

double size_cost(fir::Function &func, const Machine &m) {
  double total = 0.0;
  for (auto bb : func.get_bbs()) {
    total += size_cost(bb, m);
  }
  return total;
}

RegisterPressureEstimate::RegisterPressureEstimate(fir::BasicBlock bb) {
  TSet<fir::ValueR> live;
  const auto &instrs = bb->instructions;
  for (size_t i = instrs.size(); i-- > 0;) {
    const fir::Instr instr = instrs[i];
    if (instr->has_one_result()) {
      live.erase(instr.result());
    }
    for (const auto &arg : instr->args) {
      if (arg.is_instr() || arg.is_bb_arg()) {
        live.insert(arg);
      }
    }
    max_pressure_ = std::max(max_pressure_, static_cast<u32>(live.size()));
  }
}

}  // namespace foptim::optim::cost_model
