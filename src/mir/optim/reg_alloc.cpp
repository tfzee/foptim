#include "reg_alloc.hpp"
#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/todo.hpp"

namespace foptim::fmir {

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

  [[nodiscard]] constexpr Loc min(const Loc &other) const {
    if (bb_indx < other.bb_indx) {
      return *this;
    }
    if (bb_indx > other.bb_indx) {
      return other;
    }
    if (instr_indx < other.instr_indx) {
      return *this;
    }
    return other;
  }
  constexpr bool operator<(const Loc &other) const {
    if (bb_indx < other.bb_indx) {
      return true;
    }
    if (bb_indx > other.bb_indx) {
      return false;
    }
    return instr_indx < other.instr_indx;
  }
};

struct LinearRange {
  Loc start = {};
  Loc end = {};

  static LinearRange inBB(const MFunc &func, u32 bb_id) {
    LinearRange range{};
    range.start.bb_indx = bb_id;
    range.start.instr_indx = 0;
    range.end.bb_indx = bb_id;
    range.end.instr_indx = func.bbs[bb_id].instrs.size();
    return range;
  }

  [[nodiscard]] constexpr bool collide(const LinearRange &o) const {
    bool overlap1 = o.start < end;
    bool overlap2 = start < o.end;

    return overlap1 && overlap2;
  }

  void update(Loc new_loc) {
    start = start.min(new_loc);
    end = end.max(new_loc);
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

  constexpr void update(LinearRange new_range) { ranges.push_back(new_range); }
  constexpr void update(const LinearRangeSet &new_ranges) {
    for (const auto &new_range : new_ranges.ranges) {
      ranges.push_back(new_range);
    }
  }
};

TMap<VReg, LinearRangeSet> linear_lifetime(const MFunc &func) {
  std::vector<VReg> all_used_regs;

  for (u32 bb_i = 0; bb_i < func.bbs.size(); bb_i++) {
    const auto &bb = func.bbs[bb_i];
    for (u32 in_i = 0; in_i < bb.instrs.size(); in_i++) {
      const auto &instr = bb.instrs[in_i];
      for (u32 arg_i = 0; arg_i < instr.n_args; arg_i++) {
        const auto &arg = instr.args[arg_i];
        switch (arg.type) {
        case MArgument::ArgumentType::Imm:
        case MArgument::ArgumentType::Label:
        case MArgument::ArgumentType::MemLabel:
        case MArgument::ArgumentType::MemImmLabel:
        case MArgument::ArgumentType::MemImm:
          break;
        case MArgument::ArgumentType::VReg:
        case MArgument::ArgumentType::MemVReg:
        case MArgument::ArgumentType::MemImmVReg:
          all_used_regs.push_back(arg.reg);
          // ranges[arg.reg].update({bb_i, in_i});
          break;
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale:
          all_used_regs.push_back(arg.reg);
          // ranges[arg.reg].update({bb_i, in_i});
          all_used_regs.push_back(arg.indx);
          // ranges[arg.indx].update({bb_i, in_i});
          break;
        case MArgument::ArgumentType::MemImmVRegScale:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          ASSERT(false);
        }
      }
    }
  }
  CFG cfg{func};
  LiveVariables live{cfg, func};
  TMap<VReg, LinearRangeSet> ranges;
  for (const auto &reg : all_used_regs) {
    auto reg_id = reg_to_uid(reg);
    for (size_t i = 0; i < func.bbs.size(); i++) {
      if (live._live[i][reg_id]) {
        ranges[reg].update(LinearRange::inBB(func, i));
      }
    }
  }
  // for (auto reg : all_used_regs) {
  // utils::Debug << "reg: " << reg << "\n";
  //   for (auto active_block : live._live[reg_to_uid(reg)]) {
  //     ranges[reg].update(LinearRange::inBB(func, active_block));
  //   }
  // }
  return ranges;
}

void replace_args(MInstr &instr, const TMap<size_t, VRegType> &reg_mapping) {
  for (u32 i = 0; i < instr.n_args; i++) {
    switch (instr.args[i].type) {
    case MArgument::ArgumentType::Imm:
    case MArgument::ArgumentType::Label:
    case MArgument::ArgumentType::MemLabel:
    case MArgument::ArgumentType::MemImmLabel:
    case MArgument::ArgumentType::MemImm:
      break;
    case MArgument::ArgumentType::VReg:
    case MArgument::ArgumentType::MemImmVReg:
    case MArgument::ArgumentType::MemVReg: {
      auto reg = instr.args[i].reg;
      if (!reg.info.is_pinned() && reg_mapping.contains(reg.id)) {
        instr.args[i].reg.info.ty = reg_mapping.at(reg.id);
      }
      break;
    }
    case MArgument::ArgumentType::MemVRegVRegScale:
    case MArgument::ArgumentType::MemImmVRegVReg:
    case MArgument::ArgumentType::MemVRegVReg: {
      auto reg = instr.args[i].reg;
      auto indx = instr.args[i].indx;
      if (!reg.info.is_pinned() && reg_mapping.contains(reg.id)) {
        instr.args[i].reg.info.ty = reg_mapping.at(reg.id);
      }
      if (!indx.info.is_pinned() && reg_mapping.contains(indx.id)) {
        instr.args[i].indx.info.ty = reg_mapping.at(indx.id);
      }
      break;
    }
    case MArgument::ArgumentType::MemImmVRegScale:
    case MArgument::ArgumentType::MemImmVRegVRegScale:
      TODO("");
    }
  }
}

bool reg_is_legal(const VReg &reg, VRegType avail_reg) {
  ASSERT(!reg.info.is_pinned());

  switch (reg.info.reg_class) {
  case VRegClass::INVALID:
    TODO("UNREACh");
  case VRegClass::GeneralPurpose:
    return avail_reg == VRegType::A || avail_reg == VRegType::B ||
           avail_reg == VRegType::C || avail_reg == VRegType::D ||
           avail_reg == VRegType::DI || avail_reg == VRegType::SI ||
           avail_reg == VRegType::SP || avail_reg == VRegType::BP ||
           avail_reg == VRegType::R8 || avail_reg == VRegType::R9 ||
           avail_reg == VRegType::R10 || avail_reg == VRegType::R11 ||
           avail_reg == VRegType::R12 || avail_reg == VRegType::R13 ||
           avail_reg == VRegType::R14 || avail_reg == VRegType::R15;
  case VRegClass::Float:
    return avail_reg == VRegType::mm0 || avail_reg == VRegType::mm1 ||
           avail_reg == VRegType::mm2 || avail_reg == VRegType::mm3 ||
           avail_reg == VRegType::mm4 || avail_reg == VRegType::mm5 ||
           avail_reg == VRegType::mm6 || avail_reg == VRegType::mm7 ||
           avail_reg == VRegType::mm8 || avail_reg == VRegType::mm9 ||
           avail_reg == VRegType::mm10 || avail_reg == VRegType::mm11 ||
           avail_reg == VRegType::mm12 || avail_reg == VRegType::mm13 ||
           avail_reg == VRegType::mm14 || avail_reg == VRegType::mm15;
  }
}

constexpr size_t N_REGS_SELECTABLE = 30;
static_assert((size_t)VRegType::N_REGS - 3 == N_REGS_SELECTABLE);

constexpr void get_reg_order(MFunc &func, VRegType *regs) {
  static constexpr VRegType leaf_optimized_regs[N_REGS_SELECTABLE] = {
      VRegType::A,    VRegType::D,    VRegType::C,    VRegType::DI,
      VRegType::SI,   VRegType::R8,   VRegType::R9,   VRegType::R10,
      VRegType::R11,  VRegType::mm0,  VRegType::mm1,  VRegType::mm2,
      VRegType::mm3,  VRegType::mm4,  VRegType::mm5,  VRegType::mm6,
      VRegType::mm7,  VRegType::R12,  VRegType::R13,  VRegType::R14,
      VRegType::R15,  VRegType::B,    VRegType::mm8,  VRegType::mm9,
      VRegType::mm10, VRegType::mm11, VRegType::mm12, VRegType::mm13,
      VRegType::mm14, VRegType::mm15};
  static constexpr VRegType basic_regs[N_REGS_SELECTABLE] = {
      VRegType::B,    VRegType::R12,  VRegType::R13,  VRegType::R14,
      VRegType::R15,  VRegType::mm8,  VRegType::mm9,  VRegType::mm10,
      VRegType::mm11, VRegType::mm12, VRegType::mm13, VRegType::mm14,
      VRegType::mm15, VRegType::A,    VRegType::D,    VRegType::C,
      VRegType::DI,   VRegType::SI,   VRegType::R8,   VRegType::R9,
      VRegType::R10,  VRegType::R11,  VRegType::mm0,  VRegType::mm1,
      VRegType::mm2,  VRegType::mm3,  VRegType::mm4,  VRegType::mm5,
      VRegType::mm6,  VRegType::mm7};

  bool is_leaf = true;
  for (auto &bb : func.bbs) {
    for (auto &instr : bb.instrs) {
      if (instr.op == Opcode::call || instr.op == Opcode::invoke) {
        is_leaf = false;
        break;
      }
    }
    if (!is_leaf) {
      break;
    }
  }

  const VRegType *selected = basic_regs;
  if (is_leaf) {
    selected = leaf_optimized_regs;
  }

  for (size_t i = 0; i < N_REGS_SELECTABLE; i++) {
    regs[i] = selected[i];
  }
}

void apply_func(MFunc &func) {
  ZoneScopedN("Allocating Func");
  TMap<VRegType, LinearRangeSet> lifeness;
  lifeness.reserve(32);
  VRegType regs[N_REGS_SELECTABLE];
  TMap<size_t, VRegType> reg_mapping;
  // {
  //   ZoneScopedN("Args");
  //   for (auto &reg : func.args) {
  //     if (reg.info.is_pinned()) {
  //       reg_mapping.insert({reg.id, reg.info.ty});
  //     }
  //   }
  //   for (auto &bb : func.bbs) {
  //     for (auto &instr : bb.instrs) {
  //       replace_args(instr, reg_mapping);
  //     }
  //   }
  //   reg_mapping.clear();
  // }

  const auto lifetimes = linear_lifetime(func);
  get_reg_order(func, regs);

  {
    ZoneScopedN("Actual Alloc");
    for (auto [reg, lifetime] : lifetimes) {
      if (reg.info.is_pinned()) {
        lifeness.insert({reg.info.ty, lifetime});
        // reg_mapping.insert({reg.id, reg.info.ty});
      }
    }

    for (auto [reg, lifetime] : lifetimes) {
      if (reg.info.is_pinned()) {
      } else {
        bool found = false;
        for (auto avail_reg : regs) {
          if (!reg_is_legal(reg, avail_reg)) {
            continue;
          }

          if (!lifeness.contains(avail_reg)) {
            lifeness.insert({avail_reg, lifetime});
            reg_mapping.insert({reg.id, avail_reg});
            found = true;
            break;
          }
          if (!lifeness.at(avail_reg).collide(lifetime)) {
            lifeness.at(avail_reg).update(lifetime);
            reg_mapping.insert({reg.id, avail_reg});
            found = true;
            break;
          }
        }
        if (!found) {
          TODO("spill it ?");
          ASSERT(false);
        }
      }
    }
  }

  for (auto &bb : func.bbs) {
    for (auto &instr : bb.instrs) {
      replace_args(instr, reg_mapping);
    }
  }
}

void RegAlloc::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("RegAlloc");
  // FVec<utils::BitSet> used_regs;
  // used_regs.resize(funcs.size(), utils::BitSet::empty(12));

  for (auto &func : funcs) {
    apply_func(func);
  }
}

} // namespace foptim::fmir
