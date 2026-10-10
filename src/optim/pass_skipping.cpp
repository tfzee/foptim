#include "optim/pass_skipping.hpp"

#include <fmt/base.h>

#include <algorithm>

namespace foptim::optim {

void CleanTracker::print_stats() const {
  if constexpr (!SkipStatistics) {
    return;
  }
  if (n_passes == 0) {
    return;
  }
  FVec<size_t> order;
  for (size_t i = 0; i < n_passes; i++) {
    order.push_back(i);
  }
  std::ranges::sort(order, [this](size_t a, size_t b) {
    return counters[a].skipped.load() > counters[b].skipped.load();
  });
  u64 tot_runs = 0;
  u64 tot_skipped = 0;
  fmt::println(stderr, "FIR pass skipping ({}):", enabled ? "on" : "off");
  fmt::println(stderr, "  {: <20} {: >8} {: >8} {: >7} {: >8} {: >12}", "pass",
               "executed", enabled ? "skipped" : "w.skip", "skip%", "changed", "conserv.");
  for (auto i : order) {
    const u64 skipped = counters[i].skipped.load();
    const u64 runs = counters[i].runs.load();
    const u64 executed = enabled ? runs - skipped : runs;
    tot_runs += runs;
    tot_skipped += skipped;
    fmt::println(stderr, "  {: <20} {: >8} {: >8} {: >6.1f}% {: >8} {: >12}",
                 names[i], executed, skipped,
                 runs == 0
                     ? 0.0
                     : 100.0 * static_cast<double>(skipped) /
                           static_cast<double>(counters[i].runs),
                 counters[i].changed.load(), counters[i].conservative.load());
  }
  fmt::println(stderr, "  total: {} of {} pass runs {} ({:.1f}%)", tot_skipped,
               tot_runs, enabled ? "skipped" : "would be skipped",
               tot_runs == 0 ? 0.0
                             : 100.0 * static_cast<double>(tot_skipped) /
                                   static_cast<double>(tot_runs));
}

} // namespace foptim::optim
