
#pragma once
#include "ir/function.hpp"
#include "optim/analysis/AnalysisManager.hpp"
#include "optim/function_pass.hpp"
#include <fmt/base.h>

namespace foptim::optim {
class VerifyFunc final : public FunctionPass {
public:
  PreservedAnalysis apply(fir::Context & /*ctx*/,
                          fir::Function &func) override {
    if(!func.verify()){
      fmt::println("{}", func);
      TODO("FAILED VERIFY");
    }
    return PreservedAnalysis::all();
  }
};
} // namespace foptim::optim
