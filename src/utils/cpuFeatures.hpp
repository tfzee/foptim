#pragma once
#include <config/compiler_config.hpp>

namespace foptim {

static void populateNativeConfig(foptim::conf::Target::Features& features) {
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
}

static void populateConfig(foptim::conf::Target::Features& features,
                           const std::string_view targetName) {
  if (targetName == "native") {
    populateNativeConfig(features);
  } else {
    TODO("dont support this target yet");
  }
}

}  // namespace foptim
