#pragma once
#include <fmt/base.h>
#include <fmt/std.h>

#include "../function_pass.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

// using BBData = TVec<BitSet>;

class LVN final : public FunctionPass {
 public:
  void apply(fir::Context & /*unused*/, fir::Function &func) override;
};
}  // namespace foptim::optim
