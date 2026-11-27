#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "config/compiler_config.hpp"
#include "optim/cost_model/x86_op_class.hpp"

namespace foptim::optim::cost_model {
// one microarchitecture of target data. a new one is an enumerator, a csv,
// a cache csv, optional core and tune csvs and entries in CMakeLists.txt with
// the same name. keys and rows a csv leaves out come from Generic
enum class MicroArch : u8 {
  // not a real cpu, the median of the others, used when no cpu is requested
  Generic,
  Zen4,
  // Alder Lake / Raptor Lake P cores
  GoldenCove,
  // Arrow Lake / Lunar Lake P cores
  LionCove,
};

// config names of the microarchitectures, lower case ("zen4", "goldencove")
[[nodiscard]] std::string_view microarch_name(MicroArch arch);
[[nodiscard]] std::optional<MicroArch> parse_microarch(std::string_view name);
// every profile, Generic first
[[nodiscard]] std::span<const MicroArch> all_microarchs();
// all names, for error messages
[[nodiscard]] std::string microarch_names();

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
  // sustained bytes per cycle one core streams in from this level into the one
  // above it
  double bandwidth = 0;
};

// core parameters of a profile (data/<arch>_core.csv, keys missing there come
// from generic_core.csv). the generator emits designated initializers, so a
// csv key needs a field of the same name here
struct CoreParams {
  u32 issue_width;
  u32 retire_width;
  u32 n_alu_ports;
  u32 load_ports;
  u32 store_ports;
  // cycles lost on a mispredicted branch
  u32 mispredict_penalty;
  // cycles a taken branch costs even when predicted
  u32 taken_branch_cost;
  u32 n_gp_regs;
  // xmm/ymm registers without avx512, which doubles them
  u32 n_vec_regs;
  // registers a zmm takes, 2 where it is double pumped
  u32 zmm_parts;
  // vpmaskmov stores are microcoded and slow
  bool vpmaskmov_store_microcoded;
};

// fitted knobs (data/<arch>_tune.csv), same fallback as the core parameters
struct TuneParams {
  // weight of the size term in SizeAndLatency, a byte matters less than a cycle
  double size_weight;
};

// L1D, L2, LLC
inline constexpr u32 kNumCacheLevels = 3;

// wraps conf::Target::Features instead of redeclaring flags, new features only
// need branches in fir_lowering_estimate.cpp
class Machine {
 public:
  MicroArch arch = MicroArch::Generic;
  const conf::Target::Features *isa = nullptr;
  u32 n_ports = kNumPorts;
  // general purpose registers available to the allocator
  u32 n_gp_regs = 14;
  // xmm/ymm registers, AVX-512 extends to 16-31
  u32 n_vec_regs = 16;

  [[nodiscard]] const CoreParams &core() const;
  [[nodiscard]] const TuneParams &tune() const;
  [[nodiscard]] static Machine from_target(const conf::Target &target);
  [[nodiscard]] const TableRow &table(X86OpClass cls) const;
  // level 0 is L1D
  [[nodiscard]] const CacheLevel &cache(u32 level) const;
  // cycles of a load that misses every level
  [[nodiscard]] double mem_latency() const;
  // sustained bytes per cycle of streaming from memory
  [[nodiscard]] double mem_bandwidth() const;

 private:
  // table values with the sizes of the target config applied
  CacheLevel caches_[kNumCacheLevels];
  double mem_latency_ = 0;
  double mem_bandwidth_ = 0;
};

}  // namespace foptim::optim::cost_model
