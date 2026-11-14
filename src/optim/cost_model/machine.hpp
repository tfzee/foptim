#pragma once
#include "config/compiler_config.hpp"
#include "optim/cost_model/x86_op_class.hpp"

namespace foptim::optim::cost_model {
// one microarchitecture of target data. a new one is an enumerator, a csv
// and a COST_MICROARCHS entry in CMakeLists.txt with the same name
enum class MicroArch : u8 {
  Zen4,
};

// abstract issue ports, coarse resolution, not a mapping onto a real core
inline constexpr u32 kNumPorts = 4;

// one level of the data cache hierarchy
struct CacheLevel {
  // bytes
  u32 size = 0;
  u32 line_size = 0;
  // ways, 0 = unknown
  u32 assoc = 0;
  // cycles for a load that hits at this level
  double latency = 0;
};

// L1D, L2, LLC
inline constexpr u32 kNumCacheLevels = 3;

// wraps conf::Target::Features instead of redeclaring flags, new features only
// need branches in fir_lowering_estimate.cpp
class Machine {
 public:
  MicroArch arch = MicroArch::Zen4;
  const conf::Target::Features *isa = nullptr;
  u32 n_ports = kNumPorts;
  // general purpose registers available to the allocator
  u32 n_gp_regs = 14;
  // xmm/ymm registers, AVX-512 extends to 16-31
  u32 n_vec_regs = 16;

  [[nodiscard]] static Machine from_target(const conf::Target &target);
  [[nodiscard]] const TableRow &table(X86OpClass cls) const;
  // level 0 is L1D
  [[nodiscard]] const CacheLevel &cache(u32 level) const;
  // cycles of a load that misses every level
  [[nodiscard]] double mem_latency() const;
};

}  // namespace foptim::optim::cost_model
