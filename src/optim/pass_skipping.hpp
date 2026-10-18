#pragma once
#include <atomic>
#include <memory>
#include <string_view>

#include "config/compiler_config.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "utils/bitset.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

// Per pass counters runs, skipped, changed, ... and the table printed at the
// end of the FIR pipeline with --time_passes.
#ifdef OPTIM_STATS
inline constexpr bool SkipStatistics = true;
#else
inline constexpr bool SkipStatistics = false;
#endif

/*
Remembers which FIR function passes are known to be a no-op on a function.

A deterministic function pass that changed nothing will change nothing again
until the function changes, so running it is wasted time. A pass says it changed
nothing by returning PreservedAnalysis::all(), anything else counts as a change
(assert builds check that all() really left the function untouched, and count
the passes that return something else while changing nothing).
Every function carries a bit per pass (fir::Function::clean_passes)
The pass id is PassConfig::skip_id, one per pass config in the pipeline (a pass
with `extends` is its own config, so it has its own bit).
Only the job owning a function touches its bits.
*/
class CleanTracker {
  struct Counter {
    std::atomic<u64> runs{0};
    std::atomic<u64> skipped{0};
    std::atomic<u64> changed{0};
    std::atomic<u64> conservative{0};
  };

  bool enabled = false;
  size_t n_passes = 0;
  std::unique_ptr<Counter[]> counters;
  FVec<std::string_view> names;
  FVec<u32> interprocedural_ids;

 public:
  void init(const std::vector<conf::PassConfig *> &passes, bool enable) {
    enabled = enable;
    for (auto *p : passes) {
      p->skip_id = conf::PassConfig::NoSkipId;
    }
    n_passes = 0;
    for (auto *p : passes) {
      if (p->pass_type() != conf::PassConfig::FIR_Function ||
          !p->can_skip_when_clean() ||
          p->skip_id != conf::PassConfig::NoSkipId) {
        continue;
      }
      p->skip_id = static_cast<u32>(n_passes++);
      names.push_back(p->get_name());
      if (p->interprocedural_read()) {
        interprocedural_ids.push_back(p->skip_id);
      }
    }
    if constexpr (SkipStatistics) {
      counters = std::make_unique<Counter[]>(n_passes);
    }
  }

  // whether passes are actually skipped (otherwise the bits and counters are
  // still maintained to see what would be skipped)
  [[nodiscard]] bool skipping() const { return enabled; }
  [[nodiscard]] bool is_clean(fir::Function &f, u32 id) const {
    if (id == conf::PassConfig::NoSkipId || !f.clean_passes) {
      return false;
    }
    return (*f.clean_passes)[id];
  }

  void mark_clean(fir::Function &f, u32 id) const {
    if (id == conf::PassConfig::NoSkipId) {
      return;
    }
    if (!f.clean_passes) {
      f.clean_passes =
          std::make_unique<utils::BitSet<u64, utils::IRAlloc<u64>>>(n_passes,
                                                                    false);
    }
    (*f.clean_passes)[id].set(true);
  }

  static void mark_dirty(fir::Function &f) {
    if (f.clean_passes) {
      f.clean_passes->reset(false);
    }
  }

  // some function changed drop what depended on other functions
  void mark_dirty_interprocedural(fir::Context &ctx) const {
    if (interprocedural_ids.empty()) {
      return;
    }
    for (auto *f : ctx->storage.functions.all()) {
      if (!f->clean_passes) {
        continue;
      }
      for (auto id : interprocedural_ids) {
        (*f->clean_passes)[id].set(false);
      }
    }
  }

  void count_run(u32 id) {
    if constexpr (SkipStatistics) {
      if (id != conf::PassConfig::NoSkipId) {
        counters[id].runs++;
      }
    }
  }
  void count_skip(u32 id) {
    if constexpr (SkipStatistics) {
      counters[id].skipped++;
    }
  }
  void count_result(u32 id, bool changed, bool conservative_change) {
    if constexpr (SkipStatistics) {
      if (id == conf::PassConfig::NoSkipId) {
        return;
      }
      if (changed) {
        counters[id].changed++;
      }
      if (conservative_change) {
        counters[id].conservative++;
      }
    }
  }

  void print_stats() const;
};

}  // namespace foptim::optim
