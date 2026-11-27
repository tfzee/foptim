#include "optim/cost_model/seq_cost.hpp"

#include <algorithm>

#include "utils/todo.hpp"

namespace foptim::optim::cost_model {
u32 OpSeq::add(X86OpClass cls, std::initializer_list<u32> deps) {
  return add_with_latency(cls, -1.0, deps);
}

u32 OpSeq::add_with_latency(X86OpClass cls, double latency,
                            std::initializer_list<u32> deps) {
  ASSERT(deps.size() <= 3);
  Op op{.cls = cls, .latency = latency};
  for (const u32 d : deps) {
    ASSERT(d < ops_.size());
    op.deps[op.n_deps++] = d;
  }
  ops_.push_back(op);
  return static_cast<u32>(ops_.size() - 1);
}

SeqCost cost_seq(const OpSeq &seq, const Machine &m) {
  TVec<double> ready(seq.ops().size(), 0.0);
  SeqCost res;
  double uops = 0.0;
  for (size_t i = 0; i < seq.ops().size(); i++) {
    const auto &op = seq.ops()[i];
    const auto &row = m.table(op.cls);
    double start = 0.0;
    for (u8 d = 0; d < op.n_deps; d++) {
      start = std::max(start, ready[op.deps[d]]);
    }
    ready[i] = start + (op.latency >= 0.0 ? op.latency : row.latency);
    res.latency = std::max(res.latency, ready[i]);
    uops += row.n_uops;
    res.size_bytes += row.size_bytes;
  }
  res.throughput = uops / static_cast<double>(m.core().issue_width);
  return res;
}
}  // namespace foptim::optim::cost_model
