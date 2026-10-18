#pragma once
#include <fmt/base.h>

#include "config/compiler_config.hpp"
#include "ir/IRLocation.hpp"
#include "ir/context.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/pass_skipping.hpp"
#include "utils/job_system.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

class ModulePass {
 public:
  struct FailureReason {
    const char *reason;
    fir::IRLocation loc;
  };

#ifdef OPTIM_STATS
  IRMap<const char *, u32> stats;
  IRVec<FailureReason> failures;
#endif

  [[nodiscard]] virtual PreservedAnalysis apply(
      fir::Context & /*unused*/, JobSheduler * /*shed*/,
      AnalysisManager & /*analyMan*/) {
    TODO("impl");
  }

  // ModulePass &apply_pass(fir::Context &ctx, JobSheduler *shed) {
  //   apply(ctx, shed);
  //   utils::TempAlloc<void *>::reset();
  //   return *this;
  // }

  ModulePass &print_failures() {
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
// class StaticModulePassManager {
//  public:
//   void apply(fir::Context &ctx, JobSheduler *shed) {
//     if (ctx.config->debug.print_optimization_failure_reasons) {
//       (Passes{}.apply_pass(ctx, shed).print_failures(), ...);
//     } else {
//       (Passes{}.apply_pass(ctx, shed), ...);
//     }
//   }
// };

class ModulePassManager {
  FVec<conf::PassConfig *> dyn_passes;

 public:
  CleanTracker *tracker = nullptr;

  void clear() { dyn_passes.clear(); }

  void push_pass(conf::PassConfig *pass) { dyn_passes.push_back(pass); }

  void apply(fir::Context &ctx, JobSheduler *shed, AnalysisManager &analMan) {
    for (auto *p : dyn_passes) {
      auto *pass = p->_construct_module_pass();
      analMan.begin_module_pass(ctx);
      PreservedAnalysis r;
      if (ctx.config->debug.time_passes) {
        auto start_time = std::chrono::high_resolution_clock::now();
        r = pass->apply(ctx, shed, analMan);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(
                        end_time - start_time)
                        .count();
        if (time > 0) {
          utils::StatCollector::get().addi(time, p->get_name(),
                                           utils::StatCollector::StatTiming);
        }
      } else {
        r = pass->apply(ctx, shed, analMan);
      }
      // all() = everything that changed was reported and only that was
      // dropped, anything else drops all cached analyses and clean bits
      if (analMan.end_module_pass(ctx, r, p->get_name()) && tracker) {
        // function passes that read other functions
        tracker->mark_dirty_interprocedural(ctx);
      }
      if (ctx.config->debug.print_optimization_failure_reasons) {
        pass->print_failures();
      }
    }
    ctx.data->storage.storage_instr.collect_garbage();
  }
};
}  // namespace foptim::optim
