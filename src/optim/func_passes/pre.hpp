#pragma once
#include "ir/function.hpp"
#include "optim/function_pass.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {
using utils::BitSet;

using BBData = TVec<BitSet<>>;

using DBBData = TVec<TVec<BitSet<>>>;

class EPathPRE final : public FunctionPass {
 public:
  void apply(fir::Context &ctx, fir::Function &func) override;
};
}  // namespace foptim::optim
