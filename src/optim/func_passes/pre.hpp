#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/function_pass.hpp"
#include "utils/arena.hpp"
#include "utils/bitset.hpp"
#include "utils/logging.hpp"
#include <algorithm>

namespace foptim::optim {
using utils::BitSet;

using BBData = TVec<BitSet<>>;

using DBBData = TVec<TVec<BitSet<>>>;

class EPathPRE final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override;
};
} // namespace foptim::optim
