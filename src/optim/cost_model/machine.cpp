#include "optim/cost_model/machine.hpp"

#include "utils/todo.hpp"

namespace foptim::optim::cost_model {

namespace {
#include "x86_cost_tables.inc"
}  // namespace

Machine Machine::from_target(const conf::Target &target) {
  Machine m;
  m.arch = MicroArch::Zen4;
  m.isa = &target.features;
  m.n_ports = kNumPorts;
  m.n_gp_regs = 14;
  m.n_vec_regs = target.features.avx512f ? 32 : 16;
  return m;
}

const TableRow &Machine::table(X86OpClass cls) const {
  return table_of(arch)[static_cast<size_t>(cls)];
}

const CacheLevel &Machine::cache(u32 level) const {
  ASSERT(level < kNumCacheLevels);
  return cache_of(arch)[level];
}

double Machine::mem_latency() const { return mem_latency_of(arch); }

}  // namespace foptim::optim::cost_model
