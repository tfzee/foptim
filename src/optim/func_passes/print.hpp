#pragma once
#include "ir/function.hpp"
#include "optim/function_pass.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {
class Print final : public FunctionPass {
public:
  void apply(fir::Context & /*ctx*/, fir::Function &func) override {
    fmt::println("{}", func);
  }
};
} // namespace foptim::optim
