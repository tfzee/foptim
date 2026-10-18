#pragma once
#include "optim/analysis/analysis_manager.hpp"
#include "optim/module_pass.hpp"

namespace foptim::optim {

// Splits every integer wider than 64 bits into little endian i64 limbs.
// Signatures, calls and returns are rewritten too: a wide argument or result
// becomes one i64 per limb at its position. Must run before the MIR stage.
class LegalizeInts final : public ModulePass {
 public:
  class Config {};

  PreservedAnalysis apply(fir::Context &ctx, JobSheduler *shed,
                          AnalysisManager &analyMan) override;
};

}  // namespace foptim::optim
