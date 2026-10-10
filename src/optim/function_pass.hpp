#pragma once
#include <atomic>
#include <chrono>
#include <fmt/base.h>

#include "config/compiler_config.hpp"
#include "ir/IRLocation.hpp"
#include "ir/context.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/pass_skipping.hpp"
#include "utils/arena.hpp"
#include "utils/job_system.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

// struct FunctionPassKey {};
// template <class DerivedT> struct FunctionPassInfo {
//   static FunctionPassInfo *ID() { return &DerivedT::Key; }
// };
class FunctionPass {
public:
  struct FailureReason {
    const char *reason;
    fir::IRLocation loc;
  };

#ifdef OPTIM_STATS
  IRMap<const char *, u32> stats;
  IRVec<FailureReason> failures;
#endif

  [[nodiscard]] virtual PreservedAnalysis apply(fir::Context & /*unused*/,
                                                fir::Function & /*unused*/) {
    TODO("impl");
  }

  // FunctionPass &apply_pass(fir::Context &ctx, fir::Function &f) {
  //   apply(ctx, f);
  //   utils::TempAlloc<void *>::reset();
  //   return *this;
  // }

  FunctionPass &print_failures() {
#ifdef OPTIM_STATS
    for (auto &reason : failures) {
      (void)reason.loc;
      fmt::println("IMPL LOCATION PRINTING: {}", reason.reason);
    }
#endif
    return *this;
  }

  void failure(FailureReason reason) {
#ifdef OPTIM_STATS
    failures.push_back(reason);
#else
    (void)reason;
#endif
  }
};

// template <class... Passes>
// class StaticFunctionPassManager {
//   template <class Pass>
//   static void apply_pass(fir::Context &ctx, fir::Function &f,
//                          bool print_failure) {
//     {
//       auto p = Pass{};
//       p.apply(ctx, f);
//       if (print_failure) {
//         p.print_failures();
//       }
//     }
//     utils::TempAlloc<void *>::reset();
//   }

//  public:
//   void apply(fir::Context &ctx) {
//     for (auto &[name, func] : ctx->storage.functions) {
//       if (func->is_decl()) {
//         continue;
//       }
//       (apply_pass<Passes>(ctx, *func,
//                           utils::print_optimization_failure_reasons),
//        ...);
//     }
//     ctx.data->storage.storage_instr.collect_garbage();
//   }
// };

// template <class... Passes> class StaticParallelFunctionPassManager {
//   template <class Pass>
//   static void apply_pass(fir::Context &ctx, fir::Function &f,
//                          bool print_failure) {
//     {
//       auto p = Pass{};
//       p.apply(ctx, f);
//       if (print_failure) {
//         p.print_failures();
//       }
//     }
//     if (utils::number_worker_threads > 0) {
//       utils::TempAlloc<void *>::reset();
//     }
//   }

// public:
//   void apply(fir::Context &ctx, JobSheduler *shed) {
//     // fmt::println("FUNC: {}", sizeof...(Passes));
//     for (auto &[name, func] : ctx->storage.functions) {
//       if (func->is_decl()) {
//         continue;
//       }
//       shed->push(nullptr, [&ctx, &func]() {
//         (apply_pass<Passes>(
//              ctx, *func,
//              ctx.config->debug.print_optimization_failure_reasons),
//          ...);
//       });
//     }
//     shed->wait_till_done();
//     ctx.data->storage.storage_instr.collect_garbage();
//     if (utils::number_worker_threads == 0) {
//       utils::TempAlloc<void *>::reset();
//     }
//     // ctx.data->storage.storage_instr.collect_garbage();
//   }
// };

class ParallelFunctionPassManager {
  FVec<conf::PassConfig *> dyn_passes;

  // returns true if the function was (potentially) changed
  static bool apply_pass(fir::Context &ctx, conf::PassConfig *conf,
                         CleanTracker &tracker, fir::Function &f,
                         bool print_failure) {
    const u32 skip_id = conf->skip_id;
    tracker.count_run(skip_id);
    if (tracker.is_clean(f, skip_id)) {
      tracker.count_skip(skip_id);
      if (tracker.skipping()) {
        return false;
      }
    }
    // temp allocations never free individually, so without this the scratch
    // memory of every pass over every function piles up until the whole pass
    // group is done (with workers the whole arena is reset below instead)
    const auto temp_mark = utils::TempAlloc<void *>::save();
    bool changed = true;
    {
      auto *pass = conf->_construct_function_pass();
#ifdef ASSERT_ENABLED
      const u64 hash_before = AnalysisManager::hash_function(f);
#endif
      PreservedAnalysis r;
      if (ctx.config->debug.time_passes) {
        auto start_time = std::chrono::high_resolution_clock::now();
        r = pass->apply(ctx, f);
        AnalysisManager::invalidate(f, r);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();
        if (time > 0) {
          utils::StatCollector::get().addi(time, conf->get_name(),
                                           utils::StatCollector::StatTiming);
        }
      } else {
        r = pass->apply(ctx, f);
        AnalysisManager::invalidate(f, r);
      }
      changed = r.mask != PreservedAnalysis::all().mask;
      bool conservative = false;
#ifdef ASSERT_ENABLED
      const u64 hash_after = AnalysisManager::hash_function(f);
      if (!changed) {
        ASSERT(AnalysisManager::verify_unchanged(f, hash_before, hash_after,
                                                 conf->get_name()));
      }
      conservative = hash_after == hash_before;
#endif
      tracker.count_result(skip_id, changed, changed && conservative);
      if (changed) {
        CleanTracker::mark_dirty(f);
      } else {
        tracker.mark_clean(f, skip_id);
      }
      if (print_failure) {
        pass->print_failures();
      }
    }
    if (ctx.config->number_worker_threads > 0) {
      utils::TempAlloc<void *>::reset();
    } else {
      utils::TempAlloc<void *>::restore(temp_mark);
    }
    return changed;
  }

public:
  CleanTracker *tracker = nullptr;

  void clear() { dyn_passes.clear(); }

  void push_pass(conf::PassConfig *pass) { dyn_passes.push_back(pass); }

  void apply(fir::Context &ctx, JobSheduler *shed,
             AnalysisManager & /*analMan*/) {
    ASSERT(tracker != nullptr);
    std::atomic<bool> any_changed{false};
    for (auto *func : ctx->storage.functions.all()) {
      if (func->is_decl()) {
        continue;
      }
      shed->push(nullptr, [this, &ctx, func, &any_changed]() {
        for (auto *pass : dyn_passes) {
          if (apply_pass(ctx, pass, *tracker, *func,
                         ctx.config->debug.print_optimization_failure_reasons)) {
            any_changed = true;
          }
        }
      });
    }
    shed->wait_till_done();
    if (any_changed) {
      tracker->mark_dirty_interprocedural(ctx);
    }
    ctx.data->storage.storage_instr.collect_garbage();
    if (ctx.config->number_worker_threads == 0) {
      utils::TempAlloc<void *>::reset();
    }
  }
};

// class FunctionPassManager {
//  public:
//   FVec<FunctionPass> dyn_passes;

//   void apply(fir::Context &ctx) {
//     for (auto &[name, func] : ctx->storage.functions) {
//       if (func->is_decl()) {
//         continue;
//       }
//       for (auto pass : dyn_passes) {
//         pass.apply(ctx, *func);
//       }
//     }
//   }
// };
} // namespace foptim::optim
