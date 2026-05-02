#pragma once
#include <fmt/base.h>

#include "arg_parsing/compiler_config.hpp"
#include "ir/IRLocation.hpp"
#include "ir/context.hpp"
#include "utils/arena.hpp"
#include "utils/job_system.hpp"
#include "utils/parameters.hpp"

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

template <class... Passes>
class StaticModulePassManager {
 public:
  void apply(fir::Context &ctx, JobSheduler *shed) {
    if (utils::print_optimization_failure_reasons) {
      (Passes{}.apply_pass(ctx, shed).print_failures(), ...);
    } else {
      (Passes{}.apply_pass(ctx, shed), ...);
    }
  }
};

class ModulePassManager {
  FVec<ModulePass *> dyn_passes;

 public:
  void push_pass(ModulePass *pass) { dyn_passes.push_back(pass); }

  void apply(fir::Context &ctx, JobSheduler *shed) {
    for (auto *pass : dyn_passes) {
      pass->apply_pass(ctx, shed);
    }
    ctx.data->storage.storage_instr.collect_garbage();
  }
};
}  // namespace foptim::optim
