#pragma once
#include "ir/function.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class SimpleVectorizer final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override;
};
} // namespace foptim::optim
