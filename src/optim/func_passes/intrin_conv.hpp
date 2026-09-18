#pragma once
#include "../function_pass.hpp"
#include "optim/analysis/AnalysisManager.hpp"
#include <fmt/base.h>

namespace foptim::optim {

/*
Try to convert intrinsic like patterns into the actual intrinsics this includes
not proper intrinsics like memcpy/memset etc
*/
class IntrinConv final : public FunctionPass {
public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {

    (void)ctx;
    (void)func;
    ZoneScopedNC("IntrinConv", COLOR_OPTIMF);
    fmt::println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n{:cd}", func);
    return PreservedAnalysis::none();
  }
};

} // namespace foptim::optim
