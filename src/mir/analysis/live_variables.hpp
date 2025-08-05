#pragma once
#include "mir/analysis/cfg.hpp"
#include "mir/func.hpp"
#include "utils/bitset.hpp"
#include "utils/set.hpp"

namespace foptim::fmir {

class LiveVariables {
 public:
  const CFG &cfg;
  TVec<utils::BitSet<>> _live;
  TVec<utils::BitSet<>> _liveIn;
  TVec<utils::BitSet<>> _liveOut;

  LiveVariables(const CFG &cfg, const fmir::MFunc &func) : cfg(cfg) {
    update(func);
  }
  bool isAlive(const VReg &reg, size_t bb_id);
  void update(const fmir::MFunc &func);
};

size_t max_vreg_id(const MFunc &func);
VReg uid_to_reg(size_t id);
size_t reg_to_uid(VReg r);
inline bool uid_is_concrete(size_t id) { return id + 1 < (size_t)CReg::N_REGS; }
// VReg uid_to_reg(size_t r);
void update_def(const MInstr &instr, utils::BitSet<> &def);

struct NextUseResult {
  bool is_write;
  bool is_read;
  size_t index;
};

// args_temp is just a helper to reduce number of number of allocations when
// repeated calls to this function
NextUseResult find_next_use(const IRVec<MInstr> &instrs, size_t search_reg_id,
                            size_t start_inst, TVec<ArgData> &args_tempr);

// LINEAR LIFETIMES AFTER

struct Loc {
  u32 bb_indx = 0;
  u32 instr_indx = 0;

  [[nodiscard]] constexpr Loc max(const Loc &other) const {
    if (bb_indx > other.bb_indx) {
      return *this;
    }
    if (bb_indx < other.bb_indx) {
      return other;
    }
    if (instr_indx > other.instr_indx) {
      return *this;
    }
    return other;
  }

  [[nodiscard]] constexpr bool operator<(const Loc &other) const {
    if (bb_indx != other.bb_indx) {
      return bb_indx < other.bb_indx;
    }
    return instr_indx < other.instr_indx;
  }

  [[nodiscard]] constexpr bool operator<=(const Loc &other) const {
    if (bb_indx != other.bb_indx) {
      return bb_indx <= other.bb_indx;
    }
    return instr_indx <= other.instr_indx;
  }
};

struct LinearRange {
  Loc start = {};
  Loc end = {};

  void dump() const {
    ASSERT(start.bb_indx == end.bb_indx);
    fmt::print("{}({}..{})", start.bb_indx, start.instr_indx, end.instr_indx);
  }

  static LinearRange inBB(u32 bb_id, u32 from, u32 to) {
    LinearRange range{};
    range.start.bb_indx = bb_id;
    range.start.instr_indx = from;
    range.end.bb_indx = bb_id;
    range.end.instr_indx = to;
    return range;
  }

  [[nodiscard]] constexpr bool collide(const LinearRange &o) const {
    bool overlap1 = o.start < end;
    bool overlap2 = start < o.end;

    return overlap1 && overlap2;
  }
};

struct LinearRangeSet {
  TVec<LinearRange> ranges;
  constexpr LinearRangeSet() : ranges({}) {}
  constexpr LinearRangeSet(LinearRange input) : ranges({input}) {}
  [[nodiscard]] constexpr bool collide(const LinearRange &o) const {
    for (const auto &range : ranges) {
      if (range.collide(o)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] constexpr bool collide(const LinearRangeSet &other) const {
    for (const auto &range : ranges) {
      for (const auto &other_range : other.ranges) {
        if (range.collide(other_range)) {
          return true;
        }
      }
    }
    return false;
  }

  void dump() const {
    for (const auto &r : ranges) {
      r.dump();
      fmt::println("; ");
    }
  }

  void update(LinearRange new_range) {
    for (auto &range : ranges) {
      if (new_range.start.bb_indx == range.end.bb_indx &&
          new_range.start.instr_indx <= range.end.instr_indx &&
          new_range.start.instr_indx >= range.start.instr_indx) {
        ASSERT(new_range.end.bb_indx == range.end.bb_indx);
        // range.end.instr_indx = new_range.end.instr_indx;
        range.end.instr_indx =
            std::max(new_range.end.instr_indx, range.end.instr_indx);
        return;
      }
      if (new_range.end.bb_indx == range.start.bb_indx &&
          new_range.end.instr_indx >= range.start.instr_indx &&
          new_range.end.instr_indx <= range.end.instr_indx) {
        ASSERT(new_range.start.bb_indx == range.start.bb_indx);
        range.start.instr_indx =
            std::min(new_range.start.instr_indx, range.start.instr_indx);
        return;
      }
    }
    ranges.push_back(new_range);
  }

  void update(const LinearRangeSet &new_ranges) {
    for (const auto &new_range : new_ranges.ranges) {
      update(new_range);
    }
  }
};

TMap<VReg, LinearRangeSet> linear_lifetime(const MFunc &func);

TMap<VReg, TSet<size_t>> reg_coll(const MFunc &func);

}  // namespace foptim::fmir
