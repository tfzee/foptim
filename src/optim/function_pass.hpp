#pragma once
#include "ir/IRLocation.hpp"
#include "ir/context.hpp"
#include "utils/arena.hpp"
#include "utils/job_system.hpp"
#include "utils/parameters.hpp"

namespace foptim::optim {

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

  virtual void apply(fir::Context & /*unused*/, fir::Function & /*unused*/) {
    TODO("impl");
  }

  FunctionPass &apply_pass(fir::Context &ctx, fir::Function &f) {
    apply(ctx, f);
    utils::TempAlloc<void *>::reset();
    return *this;
  }

  FunctionPass &print_failures() {
#ifdef OPTIM_STATS
    for (auto &reason : failures) {
      (void)reason.loc;
      fmt::println("IMPL LOCATION PRINTING: {}", reason.reason);
    }
#endif
    return *this;
  }

  inline void failure(FailureReason reason) {
#ifdef OPTIM_STATS
    failures.push_back(reason);
#else
    (void)reason;
#endif
  }
};

template <class... Passes> class StaticFunctionPassManager {
public:
  void apply(fir::Context &ctx) {
    for (auto &[name, func] : ctx->storage.functions) {
      if (func.is_decl()) {
        continue;
      }
      if (utils::print_optimization_failure_reasons) {
        (Passes{}.apply_pass(ctx, func).print_failures(), ...);
      } else {
        (Passes{}.apply_pass(ctx, func), ...);
      }
    }
    ctx.data->storage.storage_instr.collect_garbage();
  }
};

template <class... Passes> class StaticParallelFunctionPassManager {
public:
  void apply(fir::Context &ctx, JobSheduler *shed) {
    for (auto &[name, func] : ctx->storage.functions) {
      if (func.is_decl()) {
        continue;
      }
      shed->push([&ctx, &func]() {
        if (utils::print_optimization_failure_reasons) {
          (Passes{}.apply_pass(ctx, func).print_failures(), ...);
        } else {
          (Passes{}.apply_pass(ctx, func), ...);
        }
      });
    }
    shed->wait_till_done();
    ctx.data->storage.storage_instr.collect_garbage();
  }
};

class FunctionPassManager {
public:
  FVec<FunctionPass> dyn_passes;

  void apply(fir::Context &ctx) {
    for (auto &[name, func] : ctx->storage.functions) {
      if (func.is_decl()) {
        continue;
      }
      for (auto pass : dyn_passes) {
        pass.apply(ctx, func);
      }
    }
  }
};
} // namespace foptim::optim
