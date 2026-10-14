#pragma once
#include "../function_pass.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include <fmt/base.h>

/*
Try to convert intrinsic like patterns into the actual intrinsics this includes
not proper intrinsics like memcpy/memset etc
*/

namespace foptim::optim {

bool match_memsetcpy(fir::Function &func);
bool match_abs(fir::Context &ctx, fir::Function &func);
bool match_bswap(fir::Context &ctx, fir::Function &func);

class IntrinConv final : public FunctionPass {
public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {

    (void)ctx;
    (void)func;
    ZoneScopedNC("IntrinConv", COLOR_OPTIMF);
    bool modified = match_memsetcpy(func);
    modified |= match_abs(ctx, func);
    modified |= match_bswap(ctx, func);
    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};

} // namespace foptim::optim
