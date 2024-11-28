#include "reg_alloc.hpp"
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

TMap<VReg, LinearRange> linear_lifetime(const MFunc &func) {
  TMap<VReg, LinearRange> ranges;

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
          ranges[arg.reg].update({bb_i, in_i});
          break;
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale:
          ranges[arg.reg].update({bb_i, in_i});
          ranges[arg.indx].update({bb_i, in_i});
          break;
        case MArgument::ArgumentType::MemImmVRegScale:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          ASSERT(false);
        }
      }
    }
  }

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
           avail_reg == VRegType::S || avail_reg == VRegType::SP ||
           avail_reg == VRegType::BP || avail_reg == VRegType::R8 ||
           avail_reg == VRegType::R9 || avail_reg == VRegType::R10 ||
           avail_reg == VRegType::R11 || avail_reg == VRegType::R12 ||
           avail_reg == VRegType::R13 || avail_reg == VRegType::R14 ||
           avail_reg == VRegType::R15;
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

void apply_func(MFunc &func) {
  ZoneScopedN("Allocating Func");
  TMap<size_t, VRegType> reg_mapping;
  // {
  //   ZoneScopedN("Args");
  //   gen_arg_mapping(func, reg_mapping);
  //   for (auto &bb : func.bbs) {
  //     for (auto &instr : bb.instrs) {
  //       replace_args(instr, reg_mapping);
  //     }
  //   }
  //   reg_mapping.clear();
  // }

  const auto lifetimes = linear_lifetime(func);
  TMap<VRegType, LinearRange> lifeness;
  lifeness.reserve(32);
  constexpr VRegType regs[] = {
      VRegType::A,    VRegType::D,    VRegType::B,    VRegType::C,
      VRegType::S,    VRegType::R8,   VRegType::R9,   VRegType::R10,
      VRegType::R11,  VRegType::R12,  VRegType::R13,  VRegType::R14,
      VRegType::R15,  VRegType::mm0,  VRegType::mm1,  VRegType::mm2,
      VRegType::mm3,  VRegType::mm4,  VRegType::mm5,  VRegType::mm6,
      VRegType::mm7,  VRegType::mm8,  VRegType::mm9,  VRegType::mm10,
      VRegType::mm11, VRegType::mm12, VRegType::mm13, VRegType::mm14,
      VRegType::mm15};

  {
    ZoneScopedN("Actual Alloc");
    for (auto [reg, lifetime] : lifetimes) {
      if (reg.info.is_pinned()) {
        lifeness.insert({reg.info.ty, lifetime});
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
            lifeness.at(avail_reg).update(lifetime.start);
            lifeness.at(avail_reg).update(lifetime.end);
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
