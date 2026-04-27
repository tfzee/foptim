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

template <class... Passes>
class StaticFunctionPassManager {
  template <class Pass>
  static void apply_pass(fir::Context &ctx, fir::Function &f,
                         bool print_failure) {
    {
      auto p = Pass{};
      p.apply(ctx, f);
      if (print_failure) {
        p.print_failures();
      }
    }
    utils::TempAlloc<void *>::reset();
  }

 public:
  void apply(fir::Context &ctx) {
    for (auto &[name, func] : ctx->storage.functions) {
      if (func->is_decl()) {
        continue;
      }
      (apply_pass<Passes>(ctx, *func,
                          utils::print_optimization_failure_reasons),
       ...);
    }
    ctx.data->storage.storage_instr.collect_garbage();
  }
};

template <class... Passes>
class StaticParallelFunctionPassManager {
  template <class Pass>
  static void apply_pass(fir::Context &ctx, fir::Function &f,
                         bool print_failure) {
    {
      auto p = Pass{};
      p.apply(ctx, f);
      if (print_failure) {
        p.print_failures();
      }
    }
    if (utils::number_worker_threads > 0) {
      utils::TempAlloc<void *>::reset();
    }
  }

 public:
  void apply(fir::Context &ctx, JobSheduler *shed) {
    for (auto &[name, func] : ctx->storage.functions) {
      if (func->is_decl()) {
        continue;
      }
      shed->push(nullptr, [&ctx, &func]() {
        (apply_pass<Passes>(ctx, *func,
                            utils::print_optimization_failure_reasons),
         ...);
      });
    }
    shed->wait_till_done();
    ctx.data->storage.storage_instr.collect_garbage();
    if (utils::number_worker_threads == 0) {
      utils::TempAlloc<void *>::reset();
    }
    // ctx.data->storage.storage_instr.collect_garbage();
  }
};

class FunctionPassManager {
 public:
  FVec<FunctionPass> dyn_passes;

  void apply(fir::Context &ctx) {
    for (auto &[name, func] : ctx->storage.functions) {
      if (func->is_decl()) {
        continue;
      }
      for (auto pass : dyn_passes) {
        pass.apply(ctx, *func);
      }
    }
  }
};
}  // namespace foptim::optim
