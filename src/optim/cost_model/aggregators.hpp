#pragma once
#include <optional>

#include "ir/basic_block_ref.hpp"
#include "ir/function_ref.hpp"
#include "optim/cost_model/machine.hpp"

namespace foptim::optim {
class CFG;
class Dominators;
struct LoopInfo;
class ScalarEvo;
}  // namespace foptim::optim

namespace foptim::optim::cost_model {
// cheap llvm-mca style estimate of one block: the larger of the resource bound
// (uops per port over the ports) and the critical path latency through intra
// block def use edges. built on demand, not in the AnalysisManager. block
// local, values from outside are ready at 0
class BlockThroughputCost {
 public:
  explicit BlockThroughputCost(fir::BasicBlock bb, const Machine &m);

  [[nodiscard]] double resource_bound() const { return resource_bound_; }
  [[nodiscard]] double critical_path() const { return critical_path_; }
  [[nodiscard]] double estimate() const {
    return std::max(resource_bound_, critical_path_);
  }

 private:
  double resource_bound_ = 0.0;
  double critical_path_ = 0.0;
};

// per iteration cost of a loop times its trip count, a loop carried chain caps
// the iteration at its own latency. the chain walk is exact for a single block
// body with one back edge chain per header argument, otherwise a sum of block
// resource bounds
class LoopCostAnalysis {
 public:
  LoopCostAnalysis(CFG &cfg, Dominators &dom, LoopInfo &loop, ScalarEvo &evo,
                    const Machine &m, double assumed_trip_count = 100.0);

  [[nodiscard]] double iter_resource_bound() const {
    return iter_resource_bound_;
  }
  [[nodiscard]] double chain_latency() const { return chain_latency_; }
  // unset when the trip count is not constant
  [[nodiscard]] std::optional<double> trip_count() const {
    return trip_count_;
  }
  [[nodiscard]] double total_cost() const {
    const double per_iter = std::max(iter_resource_bound_, chain_latency_);
    return per_iter * trip_count_.value_or(assumed_trip_count_);
  }

 private:
  double iter_resource_bound_ = 0.0;
  double chain_latency_ = 0.0;
  std::optional<double> trip_count_;
  double assumed_trip_count_;
};

// sum of CostKind::Size over a block or function, for code growth budgets
double size_cost(fir::BasicBlock bb, const Machine &m);
double size_cost(fir::Function &func, const Machine &m);

// block local register pressure: the most FIR values live at one point, from a
// backward live set scan
class RegisterPressureEstimate {
 public:
  explicit RegisterPressureEstimate(fir::BasicBlock bb);

  [[nodiscard]] u32 max_pressure() const { return max_pressure_; }
  [[nodiscard]] bool exceeds(const Machine &m, bool vector) const {
    return max_pressure_ > (vector ? m.n_vec_regs : m.n_gp_regs);
  }

 private:
  u32 max_pressure_ = 0;
};

}  // namespace foptim::optim::cost_model
