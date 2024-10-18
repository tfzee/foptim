#pragma once
#include "../function_pass.hpp"
#include "ir/function.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

class Mem2Reg final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override;
};

} // namespace foptim::optim
