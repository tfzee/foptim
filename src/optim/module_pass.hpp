#pragma once
#include <fmt/base.h>

#include "config/compiler_config.hpp"
#include "ir/IRLocation.hpp"
#include "ir/context.hpp"
#include "utils/arena.hpp"
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

  virtual void apply(fir::Context & /*unused*/, JobSheduler * /*shed*/) {
    TODO("impl");
  }

  ModulePass &apply_pass(fir::Context &ctx, JobSheduler *shed) {
    apply(ctx, shed);
    utils::TempAlloc<void *>::reset();
    return *this;
  }

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
  void push_pass(conf::PassConfig *pass) { dyn_passes.push_back(pass); }

  void apply(fir::Context &ctx, JobSheduler *shed) {
    for (auto *p : dyn_passes) {
      auto *pass = p->_construct_module_pass();
      if (ctx.config->debug.time_passes) {
        auto start_time = std::chrono::high_resolution_clock::now();
        pass->apply(ctx, shed);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();
        if (time > 0) {
          utils::StatCollector::get().addi(time, p->get_name(),
                                           utils::StatCollector::StatTiming);
        }
      } else {
        pass->apply(ctx, shed);
      }
      if (ctx.config->debug.print_optimization_failure_reasons) {
        pass->print_failures();
      }
    }
    ctx.data->storage.storage_instr.collect_garbage();
  }
};
} // namespace foptim::optim
