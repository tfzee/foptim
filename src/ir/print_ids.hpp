#pragma once
#include <fmt/format.h>

#include <unordered_map>

#include "utils/types.hpp"

namespace foptim::fir {

// Printing of IR objects identified by their address. With
// `deterministic_ids` set every function print numbers the objects by first
// appearance (`%0`, `bb0`, ...) instead of showing the address, so the output
// is stable and can be checked by FileCheck.
struct PrintIds {
  static inline bool deterministic_ids = false;

  struct State {
    std::unordered_map<const void *, u32> ids;
    // blocks and values are numbered independently
    u32 next_bb = 0;
    u32 next_val = 0;
  };
  static State &state() {
    static thread_local State s;
    return s;
  }
  static u32 get(const void *p, bool is_bb) {
    auto &s = state();
    auto [it, inserted] = s.ids.try_emplace(p, 0);
    if (inserted) {
      it->second = is_bb ? s.next_bb++ : s.next_val++;
    }
    return it->second;
  }
  static void reset() {
    state().ids.clear();
    state().next_bb = 0;
    state().next_val = 0;
  }
};

// scopes the numbering to one function
struct PrintIdsScope {
  PrintIdsScope() {
    if (PrintIds::deterministic_ids) {
      PrintIds::reset();
    }
  }
  ~PrintIdsScope() {
    if (PrintIds::deterministic_ids) {
      PrintIds::reset();
    }
  }
};

struct IRId {
  const void *ptr;
  // prefix used in deterministic mode
  const char *prefix;
};

} // namespace foptim::fir

template <> struct fmt::formatter<foptim::fir::IRId> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(foptim::fir::IRId const &id, format_context &ctx) const {
    if (foptim::fir::PrintIds::deterministic_ids) {
      return fmt::format_to(ctx.out(), "{}{}", id.prefix,
                            foptim::fir::PrintIds::get(id.ptr, id.prefix[0] == 'b'));
    }
    return fmt::format_to(ctx.out(), "{:p}", id.ptr);
  }
};
