#include "optim/cost_model/branch_cost.hpp"

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/instruction_data.hpp"
#include "optim/cost_model/cost.hpp"
#include "optim/cost_model/fir_lowering_estimate.hpp"
#include "utils/map.hpp"

namespace foptim::optim::cost_model {
namespace {
constexpr double kColdEdge = 0.01;
constexpr double kHotEdge = 0.9;
constexpr double kNullPtrEdge = 0.25;

bool ends_in_unreachable(fir::BasicBlock bb) {
  return bb->get_terminator()->is(fir::InstrType::Unreachable);
}

// the condition is a compare of a pointer with null for equality
// returns +1 for eq (edge 0 is the null case), -1 for ne, 0 otherwise
int null_check_kind(fir::Instr term) {
  const auto cond = term->get_arg(0);
  if (!cond.is_instr()) {
    return 0;
  }
  const auto cmp = cond.as_instr();
  if (!cmp->is(fir::InstrType::ICmp) || cmp->args.size() != 2) {
    return 0;
  }
  const bool eq = cmp->is(fir::ICmpInstrSubType::EQ);
  if (!eq && !cmp->is(fir::ICmpInstrSubType::NE)) {
    return 0;
  }
  const auto is_null = [](fir::ValueR v) {
    return v.is_constant() && v.as_constant()->is_null();
  };
  if (!(is_null(cmp->args[0]) || is_null(cmp->args[1]))) {
    return 0;
  }
  return eq ? 1 : -1;
}
}  // namespace

double edge_probability(const CFG &cfg, const Dominators &dom,
                        fir::BasicBlock bb, u32 edge_idx) {
  const auto term = bb->get_terminator();
  if (!term->is(fir::InstrType::CondBranchInstr) || term->bbs.size() != 2) {
    return 0.5;
  }
  const u32 other = 1 - edge_idx;
  const auto mine = term->bbs[edge_idx].bb;
  const auto theirs = term->bbs[other].bb;
  if (ends_in_unreachable(mine) != ends_in_unreachable(theirs)) {
    return ends_in_unreachable(mine) ? kColdEdge : 1.0 - kColdEdge;
  }
  const u32 id = cfg.get_bb_id(bb);
  const bool back_mine = dom.dominates(cfg.get_bb_id(mine), id);
  const bool back_theirs = dom.dominates(cfg.get_bb_id(theirs), id);
  if (back_mine != back_theirs) {
    return back_mine ? kHotEdge : 1.0 - kHotEdge;
  }
  if (const int kind = null_check_kind(term); kind != 0) {
    // the true edge of an eq compare is the null case
    const bool edge_is_null_case = (kind > 0) == (edge_idx == 0);
    return edge_is_null_case ? kNullPtrEdge : 1.0 - kNullPtrEdge;
  }
  return 0.5;
}

ArmCost arm_cost(std::span<const fir::Instr> instrs, const Machine &m) {
  ArmCost out;
  TMap<fir::ValueR, double> finish_time;
  for (auto instr : instrs) {
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
    const double finish = ready + cost(instr, CostKind::Latency, ctx).value;
    if (instr->has_one_result()) {
      finish_time[instr.result()] = finish;
    }
    out.latency = std::max(out.latency, finish);
    out.tput += cost(instr, CostKind::Throughput, ctx).value;
  }
  return out;
}

double mispredict_rate(double p_taken) {
  return std::min(p_taken, 1.0 - p_taken);
}

IfConvertCost cost_if_convert(const IfConvertInput &in, const Machine &m) {
  const double p1 = 1.0 - in.p0;
  const auto &cmov = m.table(X86OpClass::CMOV);
  const auto &blend = m.table(X86OpClass::VEC_BLEND);
  const auto &jcc = m.table(X86OpClass::CMP_JCC_FUSED);
  const double select_tput = in.n_int_selects * cmov.recip_throughput +
                             in.n_float_selects * blend.recip_throughput;
  const double select_lat =
      in.n_float_selects > 0 ? blend.latency : cmov.latency;

  // the taken edge costs the front end one more cycle, the mispredict the
  // flush. the arm that is executed is the one that is taken
  const double branch_tput = in.p0 * in.arm0.tput + p1 * in.arm1.tput +
                             jcc.recip_throughput +
                             in.p0 * m.core().taken_branch_cost;
  const double flush = mispredict_rate(in.p0) * m.core().mispredict_penalty;
  double select = in.arm0.tput + in.arm1.tput + select_tput;
  double branch = branch_tput;
  if (in.carried) {
    select = std::max(select,
                      std::max(in.arm0.latency, in.arm1.latency) + select_lat);
    branch = std::max(branch, in.p0 * in.arm0.latency + p1 * in.arm1.latency);
  }
  IfConvertCost out;
  out.select = select;
  out.branch = branch + flush;
  return out;
}
}  // namespace foptim::optim::cost_model
