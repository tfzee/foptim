#pragma once
#include <span>

#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

struct ArgFact {
  enum class Kind : u8 { Unknown, Int, Null, NonNull };
  Kind kind = Kind::Unknown;
  u8 bits = 0;
  u64 val = 0;

  static ArgFact unknown() { return {}; }
  static ArgFact null() { return {Kind::Null, 0, 0}; }
  static ArgFact nonnull() { return {Kind::NonNull, 0, 0}; }
  static ArgFact int_(u8 bits, u64 val);

  [[nodiscard]] bool is_known() const { return kind != Kind::Unknown; }
  [[nodiscard]] bool is_const() const {
    return kind == Kind::Int || kind == Kind::Null;
  }
  bool operator==(const ArgFact &o) const = default;
};

// One fact per argument of the call.
void facts_of_call(fir::Instr call, std::span<ArgFact> out);

struct ArgGain {
  // callee size after propagation, counted like n_instrs, a lower bound when
  // exceeded
  u32 remaining = 0;
  u32 folded = 0;
  // instructions in blocks no feasible edge reaches
  u32 dead = 0;
  u32 branches_folded = 0;
  bool exceeded = false;
};

// One pass over the callee in reverse post order, like SCCP without a
// fixpoint. Loop headers, memory and calls are unknown, so it never claims
// more than the real cleanup. Stops once remaining is above the budget.
ArgGain estimate_arg_gain(fir::Function &callee, std::span<const ArgFact> facts,
                          u32 budget);

}  // namespace foptim::optim
