#include "optim/cost_model/machine.hpp"

#include "utils/todo.hpp"

namespace foptim::optim::cost_model {

namespace {
#include "x86_cost_tables.inc"
}  // namespace

namespace {
constexpr MicroArch kAllArchs[] = {MicroArch::Generic, MicroArch::Zen4, MicroArch::GoldenCove,
                                   MicroArch::LionCove};
}  // namespace

std::string_view microarch_name(MicroArch arch) {
  switch (arch) {
    case MicroArch::Generic:
      return "generic";
    case MicroArch::Zen4:
      return "zen4";
    case MicroArch::GoldenCove:
      return "goldencove";
    case MicroArch::LionCove:
      return "lioncove";
  }
  return "";
}

std::optional<MicroArch> parse_microarch(std::string_view name) {
  for (const auto arch : kAllArchs) {
    if (name == microarch_name(arch)) {
      return arch;
    }
  }
  return std::nullopt;
}

std::span<const MicroArch> all_microarchs() { return kAllArchs; }

std::string microarch_names() {
  std::string out;
  for (const auto arch : kAllArchs) {
    if (!out.empty()) {
      out += ", ";
    }
    out += microarch_name(arch);
  }
  return out;
}

Machine Machine::from_target(const conf::Target &target) {
  Machine m;
  if (!target.tune.empty()) {
    const auto arch = parse_microarch(target.tune);
    ASSERT_M(arch.has_value(), "unknown microarchitecture in target.tune");
    m.arch = *arch;
  } else {
    m.arch = MicroArch::Generic;
  }
  for (u32 i = 0; i < kNumCacheLevels; i++) {
    m.caches_[i] = cache_of(m.arch)[i];
    const auto &o = target.cache[i];
    if (o.size != 0) {
      m.caches_[i].size = o.size;
    }
    if (o.line_size != 0) {
      m.caches_[i].line_size = o.line_size;
    }
    if (o.assoc != 0) {
      m.caches_[i].assoc = o.assoc;
    }
  }
  m.mem_latency_ = mem_latency_of(m.arch);
  m.mem_bandwidth_ = mem_bandwidth_of(m.arch);
  m.isa = &target.features;
  m.n_ports = kNumPorts;
  m.n_gp_regs = m.core().n_gp_regs;
  m.n_vec_regs = target.features.avx512f ? 2 * m.core().n_vec_regs
                                         : m.core().n_vec_regs;
  return m;
}

const TableRow &Machine::table(X86OpClass cls) const {
  return table_of(arch)[static_cast<size_t>(cls)];
}

const CoreParams &Machine::core() const { return core_of(arch); }

const TuneParams &Machine::tune() const { return tune_of(arch); }

const CacheLevel &Machine::cache(u32 level) const {
  ASSERT(level < kNumCacheLevels);
  return caches_[level];
}

double Machine::mem_latency() const { return mem_latency_; }

double Machine::mem_bandwidth() const { return mem_bandwidth_; }

}  // namespace foptim::optim::cost_model
