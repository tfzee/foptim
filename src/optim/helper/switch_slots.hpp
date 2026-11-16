#pragma once
#include <optional>
#include <span>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

struct SlotLimits {
  i128 max_entries = 1024;
  i128 max_slots_per_case = 4;
  u32 max_runs = 4;
  bool bmi2 = false;
  // perfect hash only between these, below a compare chain is as cheap and the
  // search is random above
  size_t min_hash_cases = 8;
  size_t max_hash_cases = 128;
  u32 max_hash_tries = 4096;
};

// the cases of a switch instruction
struct SwitchCases {
  struct Case {
    i128 value;
    u32 edge;
  };
  fir::Instr term;
  // in switch order
  TVec<Case> cases;
  u32 value_bits = 0;

  // none unless the switch has an integer value of 8 to 64 bits and constant
  // case values
  static std::optional<SwitchCases> parse(fir::Instr term);
  [[nodiscard]] fir::ValueR value() const { return term->args.back(); }
  [[nodiscard]] fir::BBRefWithArgs default_edge() const {
    return term->bbs.back();
  }
  // the default only has an unreachable
  [[nodiscard]] bool default_unreachable() const;
  // the target with the most cases (the first one on a tie)
  [[nodiscard]] fir::BasicBlock dominant_target(u32 &count) const;
  // the cases that go to a target, sorted by value
  [[nodiscard]] TVec<Case> of_target(fir::BasicBlock target) const;
};

// A function from the switch value to a slot 0..n_slots-1 that is unique for
// every planned case value. Values that are no case land somewhere (linear
// and runs: outside of the slot range, mul shift: on any slot), so a check is
// needed unless the default is unreachable.
class SlotMap {
 public:
  enum class Kind { Linear, Runs, Stride, Mask, MulShift };
  // consecutive values lo..lo+len-1 start at slot base
  struct Run {
    i128 lo;
    u32 len;
    u32 base;
  };

  Kind kind = Kind::Linear;
  u32 value_bits = 0;
  u64 n_slots = 0;
  // linear has one
  TVec<Run> runs;
  // mul shift: (zext(v) * mul) >> shift, stride: (v - lo) >> shift
  u64 mul = 0;
  u32 shift = 0;
  // mask: pext(zext(v), mask). With pure_mask the bits outside of the mask are
  // fixed on every case, so (zext(v) & ~mask) == fixed proves membership
  u64 mask = 0;
  u64 fixed = 0;
  bool pure_mask = false;

  // host side slot of a case value
  [[nodiscard]] u64 slot(i128 v) const;
  // a key column proves membership of mul shift and of a mask that is not pure
  [[nodiscard]] bool needs_keys() const {
    return kind == Kind::MulShift || (kind == Kind::Mask && !pure_mask);
  }
  [[nodiscard]] u32 key_size() const { return value_bits / 8; }
  // one key per slot: the case value, slots without a case hold the value of
  // some case on another slot so that no value can match there
  void fill_keys(std::span<const i128> values, u8 *out) const;

  struct Dispatch {
    fir::BasicBlock hit;
    // valid in hit
    fir::ValueR slot;
  };
  // Emits the slot computation in `from`. Every path ends in the returned
  // hit block or in `miss` (never in miss if the value is a planned case for
  // sure, then miss_possible is false). `keys` is the global of fill_keys,
  // only read if a mul shift can miss.
  Dispatch emit(fir::ContextData *ctx, fir::Builder &b, fir::ValueR value,
                fir::BasicBlock from, const fir::BBRefWithArgs &miss,
                bool miss_possible, fir::Global keys) const;
};

// values are sorted and unique. Tries one run, a stride (all values share their
// low bits), up to max_runs runs split at the largest gaps, a pext mask (needs
// bmi2) and a perfect hash, and takes the first that fits the limits.
// A slot no case maps to (a hole) holds nothing for a value that passes the
// check, so with miss_possible only maps without holes or with a key column are
// planned. With holes_ok the caller sends the holes to the default itself (a
// jump table), so every map that fits the limits is usable.
std::optional<SlotMap> plan_slots(std::span<const i128> values, u32 value_bits,
                                  const SlotLimits &limits, bool miss_possible,
                                  bool holes_ok = false);

// a zero initialised constant internal global of a table
fir::Global make_slot_global(fir::Function &func, size_t bytes, u32 align);

}  // namespace foptim::optim
