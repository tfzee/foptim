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
};

}  // namespace foptim::optim::cost_model
