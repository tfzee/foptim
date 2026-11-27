#pragma once
#include <bit>
#include <optional>
#include <span>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "optim/cost_model/machine.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

// The members of a set of integers as windows of at most 64 values, one
// constant mask each: bit k of mask is set when base + k is a member.
struct BitWindow {
  u64 base = 0;
  u32 span = 0;
  u64 mask = 0;

  // one member is an equality check, every value of the span a range check,
  // neither needs the shift
  enum class Kind { Eq, Range, Mask };
  [[nodiscard]] Kind kind() const {
    if (std::popcount(mask) == 1) {
      return Kind::Eq;
    }
    const u64 full = span >= 64 ? ~u64{0} : (u64{1} << span) - 1;
    return mask == full ? Kind::Range : Kind::Mask;
  }
  // the member of an Eq window
  [[nodiscard]] u64 member() const {
    return base + static_cast<u64>(std::countr_zero(mask));
  }
};

struct BitWindows {
  TVec<BitWindow> windows;
};

inline constexpr u32 MaxBitWindows = 4;

// values are sorted and unique. Cuts at the largest gaps with as few windows as
// possible (at most max_windows), none if a window would span more than 64.
// When every value is below 64 and there is one window its base is 0, so no
// subtraction is needed.
std::optional<BitWindows> plan_bit_windows(std::span<const u64> values,
                                           u32 max_windows = MaxBitWindows);

// Emits (or over the windows of: x - base u< span and bit (x - base) of mask),
// x is an integer of 8 to 64 bits, the result an i1. With invert the result is
// the complement, for a set that is the minority of a function whose common
// value is true.
fir::ValueR emit_bit_windows(fir::ContextData *ctx, fir::Builder &b,
                             fir::ValueR x, const BitWindows &w, bool invert);

struct LookupChoice {
  // the windows are cheaper than a load from a table of table_bytes
  bool windows = false;
  double window_score = 0.0;
  double table_score = 0.0;
};

// Prices both ways with abstract op sequences. hot: the code runs in a loop,
// so the table stays in L1 and the throughput of the iterations overlaps; cold
// the table is assumed to miss L1 and the latency of one execution counts.
LookupChoice choose_windows_or_table(const BitWindows &w, bool hot,
                                     const cost_model::Machine &machine);

// The same against n_cases equality checks that are or'd, for a value too wide
// to tabulate (table_score is the score of the compares).
LookupChoice choose_windows_or_compares(const BitWindows &w, u32 n_cases,
                                        bool hot,
                                        const cost_model::Machine &machine);

}  // namespace foptim::optim
