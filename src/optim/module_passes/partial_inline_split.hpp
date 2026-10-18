#pragma once
#include "ir/function.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/helper/region_extractor.hpp"
#include "optim/module_pass.hpp"

namespace foptim::optim {

// Splits a function that starts with a cheap guard and continues with a big
// body into a small wrapper and f.rest, so the normal inliner can inline the
// wrapper and fold the guard at the call sites that know the guarded argument.
class PartialInlineSplit final : public ModulePass {
 public:
  struct Config {
    u32 rest_min = 40;
    u32 prefix_max = 16;
    u32 max_live_ins = 8;
  };
  Config config;

  PreservedAnalysis apply(fir::Context &ctx, JobSheduler *shed,
                          AnalysisManager &analyMan) override;

 private:
  bool split(fir::Context &ctx, fir::Function &f, AnalysisManager &analyMan);
};

}  // namespace foptim::optim
