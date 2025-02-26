#pragma once
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/APInt.hpp"
#include <limits>

namespace foptim::optim {

// using utils::Int128;

class KnownStackBits final : public AttributeAnalysis {
public:
  KnownStackBits() = default;
  ~KnownStackBits() override = default;

  void materialize_impl(fir::Context &ctx) override { (void)ctx; }
  Result update_impl(AttributerManager &m, Worklist &worklist) override {
    (void)m;
    (void)worklist;
    return Result::Fixed;
  }
};
} // namespace foptim::optim
