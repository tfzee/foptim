#pragma once
#include <cpuid.h>
#include <sched.h>

#include <config/compiler_config.hpp>
#include <optional>

#include "optim/cost_model/machine.hpp"
#include "utils/verbosity.hpp"

namespace foptim {

namespace cpu_detail {

struct Regs {
  u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
};

inline Regs cpuid(u32 leaf, u32 sub = 0) {
  Regs r;
  __cpuid_count(leaf, sub, r.eax, r.ebx, r.ecx, r.edx);
  return r;
}

// what cpuid says about the core the calling thread runs on
struct HostInfo {
  bool intel = false;
  bool amd = false;
  u32 family = 0;
  u32 model = 0;
  conf::Target::CacheOverride cache[3];
};

inline HostInfo read_host_info() {
  HostInfo info;
  const auto vendor = cpuid(0);
  info.intel = vendor.ebx == 0x756e6547 && vendor.edx == 0x49656e69 &&
               vendor.ecx == 0x6c65746e;
  info.amd = vendor.ebx == 0x68747541 && vendor.edx == 0x69746e65 &&
             vendor.ecx == 0x444d4163;
  const auto ver = cpuid(1);
  info.family = (ver.eax >> 8) & 0xf;
  info.model = (ver.eax >> 4) & 0xf;
  if (info.family == 0xf) {
    info.family += (ver.eax >> 20) & 0xff;
  }
  if (info.family == 0x6 || info.family >= 0xf) {
    info.model |= ((ver.eax >> 16) & 0xf) << 4;
  }

  // intel leaf 4, amd leaf 0x8000001D, same layout. sizes are per cache
  // instance, so L1D and L2 per core and the LLC as a whole
  u32 leaf = info.intel ? 4U : 0x8000001DU;
  if (!info.intel && !info.amd) {
    return info;
  }
  if (info.amd && cpuid(0x80000000).eax < leaf) {
    return info;
  }
  for (u32 sub = 0; sub < 16; sub++) {
    const auto c = cpuid(leaf, sub);
    const u32 type = c.eax & 0x1f;
    if (type == 0) {
      break;
    }
    const u32 level = (c.eax >> 5) & 0x7;
    // data or unified
    if ((type != 1 && type != 3) || level < 1 || level > 3) {
      continue;
    }
    const u32 line = (c.ebx & 0xfff) + 1;
    const u32 parts = ((c.ebx >> 12) & 0x3ff) + 1;
    const u32 ways = ((c.ebx >> 22) & 0x3ff) + 1;
    const u32 sets = c.ecx + 1;
    auto &out = info.cache[level - 1];
    out.size = line * parts * ways * sets;
    out.line_size = line;
    out.assoc = ways;
  }
  return info;
}

// hybrid cpus report a different cache and core type per core. the P core is
// the one the cost model tables describe, so the query runs pinned on one so
// the result does not depend on where the scheduler put the compiler
inline HostInfo read_host_info_pinned() {
  const bool hybrid = (cpuid(7).edx & (1U << 15)) != 0;
  if (!hybrid || cpuid(0).eax < 0x1a) {
    return read_host_info();
  }
  cpu_set_t saved;
  if (sched_getaffinity(0, sizeof(saved), &saved) != 0) {
    return read_host_info();
  }
  std::optional<HostInfo> found;
  for (int cpu = 0; cpu < CPU_SETSIZE && !found; cpu++) {
    if (!CPU_ISSET(cpu, &saved)) {
      continue;
    }
    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(cpu, &one);
    if (sched_setaffinity(0, sizeof(one), &one) != 0) {
      continue;
    }
    // 0x40 core, 0x20 atom
    if ((cpuid(0x1a).eax >> 24) == 0x40) {
      found = read_host_info();
    }
  }
  sched_setaffinity(0, sizeof(saved), &saved);
  return found ? *found : read_host_info();
}

// closest table, an unknown cpu of a vendor takes the newest table of it
inline optim::cost_model::MicroArch pick_microarch(const HostInfo &info) {
  using optim::cost_model::MicroArch;
  if (info.intel && info.family == 6) {
    switch (info.model) {
      // arrow lake, lunar lake
      case 0xb5:
      case 0xc5:
      case 0xc6:
      case 0xbd:
        return MicroArch::LionCove;
      default:
        return MicroArch::GoldenCove;
    }
  }
  if (info.intel) {
    return MicroArch::GoldenCove;
  }
  return MicroArch::Zen4;
}

}  // namespace cpu_detail

static void populateNativeConfig(foptim::conf::Target &target) {
  auto &features = target.features;
  __builtin_cpu_init();
  ASSERT(__builtin_cpu_supports("avx2"));
  features.avx512f = __builtin_cpu_supports("avx512f");
  features.avx512bw = __builtin_cpu_supports("avx512bw");
  features.avx512cd = __builtin_cpu_supports("avx512cd");
  features.avx512dq = __builtin_cpu_supports("avx512dq");
  features.avx512vl = __builtin_cpu_supports("avx512vl");
  features.bmi2 = __builtin_cpu_supports("bmi2");
  features.avx512vpopcntdq = __builtin_cpu_supports("avx512vpopcntdq");
  features.avx512bitalg = __builtin_cpu_supports("avx512bitalg");
  features.avx512vbmi = __builtin_cpu_supports("avx512vbmi");
  features.avx512ifma = __builtin_cpu_supports("avx512ifma");
  features.avxvnni = __builtin_cpu_supports("avxvnni");

  const auto info = cpu_detail::read_host_info_pinned();
  const auto arch = cpu_detail::pick_microarch(info);
  target.tune = std::string(optim::cost_model::microarch_name(arch));
  for (u32 i = 0; i < 3; i++) {
    target.cache[i] = info.cache[i];
  }
  if (utils::verbose()) {
    fmt::println(stderr,
                 "native: tune {} (family {:#x} model {:#x}), L1D {} L2 {} "
                 "LLC {} bytes",
                 target.tune, info.family, info.model, info.cache[0].size,
                 info.cache[1].size, info.cache[2].size);
  }
}

static void populateConfig(foptim::conf::Target &target,
                           const std::string_view targetName) {
  if (targetName == "native") {
    populateNativeConfig(target);
  } else {
    TODO("dont support this target yet");
  }
}

}  // namespace foptim
