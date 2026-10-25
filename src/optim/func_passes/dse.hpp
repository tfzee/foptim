#pragma once

#include "../function_pass.hpp"

namespace foptim::optim {

// Removes stores, memsets and memcpys whose bytes are overwritten on every path
// before anything reads them, and the ones into a stack object that never
// escapes and is not read again.
class DSE final : public FunctionPass {
 public:
  struct Config {};
  Config config;

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
