#pragma once
#include <initializer_list>

#include "optim/cost_model/machine.hpp"
#include "optim/cost_model/x86_op_class.hpp"
#include "utils/vec.hpp"

namespace foptim::optim::cost_model {
// a short dependency graph of abstract x86 ops, for choosing between two ways
// to compute one value before any instruction of either exists
class OpSeq {
 public:
  struct Op {
    X86OpClass cls;
    // negative: the latency of the table row
    double latency = -1.0;
    u8 n_deps = 0;
    u32 deps[3] = {};
  };

  // deps are the indices of earlier ops, returns the index of the new op
  u32 add(X86OpClass cls, std::initializer_list<u32> deps = {});
  // an op with a latency of its own, a load that hits a given cache level
  u32 add_with_latency(X86OpClass cls, double latency,
                       std::initializer_list<u32> deps = {});
  // an op that is free of dependencies and costs a uop (a constant materialized
  // by movabs)
  u32 add_const() { return add(X86OpClass::MOV_RR); }

  [[nodiscard]] const TVec<Op> &ops() const { return ops_; }

 private:
  TVec<Op> ops_;
};

struct SeqCost {
  // longest dependent chain, values from outside are ready at 0
  double latency = 0.0;
  // uops over the issue width
  double throughput = 0.0;
  double size_bytes = 0.0;
  // cycles of one execution when the surrounding code is not the limit
  [[nodiscard]] double cycles() const {
    return latency > throughput ? latency : throughput;
  }
};

[[nodiscard]] SeqCost cost_seq(const OpSeq &seq, const Machine &m);
}  // namespace foptim::optim::cost_model
