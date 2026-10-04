#pragma once
#include <fmt/core.h>

#include "optim/analysis/analysis_manager.hpp"
#include "utils/tracy.hpp"

#include "optim/module_pass.hpp"

namespace foptim::optim {

PreservedAnalysis merge_func_dups(fir::Context &ctx, JobSheduler *shed);
PreservedAnalysis merge_func_dups_only_same(fir::Context &ctx);

template <bool onlySame> class FunctionDeDup final : public ModulePass {
public:
  PreservedAnalysis apply(fir::Context &ctx, JobSheduler *shed,
                          AnalysisManager & /*analyMan*/) override {
    ZoneScopedN("FunctionDeDup");
    // TODO maybe run always onlysame before hand ??
    // but then we iterate twice over everything??
    if constexpr (onlySame) {
      return merge_func_dups_only_same(ctx);
    } else {
      return merge_func_dups(ctx, shed);
    }

  }
};
} // namespace foptim::optim
