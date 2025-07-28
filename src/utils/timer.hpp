#pragma once
#include <fmt/core.h>

#include <chrono>
#include <unordered_map>

#include "utils/types.hpp"

namespace foptim::utils {

struct ScopedTimer;

struct DumbTimer {
  std::unordered_map<const char *, u64> times;

  void print() {
    for (const auto &[name, time] : times) {
      if (time > 100'000'000) {
        fmt::println("{:<20} {:>10.3f}  s", name, (double)time / 1'000'000'000);
      } else if (time > 10'000) {
        fmt::println("{:<20} {:>10.3f} ms", name, (double)time / 1'000'000);
      } else {
        fmt::println("{:<20} {:>6}     ns", name, time);
      }
    }
  }

  ScopedTimer scopedTimer(const char *name);
};

struct ScopedTimer {
  DumbTimer *timer;
  const char *name;
  std::chrono::high_resolution_clock::time_point start;

  ScopedTimer(DumbTimer *t, const char *name)
      : timer(t),
        name(name),
        start(std::chrono::high_resolution_clock::now()) {}

  ~ScopedTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    timer->times.insert({name, time});
  }
};

ScopedTimer DumbTimer::scopedTimer(const char *name) { return {this, name}; }
}  // namespace foptim::utils
