#include "reg_alloc.hpp"
#include "mir/instr.hpp"
#include "utils/bitset.hpp"
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

FMap<VReg, LinearRange> linear_lifetime(const MFunc &func) {
  FMap<VReg, LinearRange> ranges;

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

void replace_args(MInstr &instr, const FMap<size_t, VRegType> &reg_mapping) {
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

void gen_arg_mapping(MFunc &func, FMap<size_t, VRegType> & /*unused*/) {
  // TODO: depends on calling conv
  for (u32 arg_i = 0; arg_i < func.args.size(); arg_i++) {
    ASSERT(!func.args[arg_i].info.is_pinned());

    auto arg_ty = func.arg_tys[arg_i];
    auto instr = MInstr(Opcode::mov, MArgument{func.args[arg_i], arg_ty},
                        MArgument::Mem(VReg::RSP(), 8 * (arg_i + 2), arg_ty));
    func.bbs[0].instrs.insert(func.bbs[0].instrs.begin(), instr);
  }
}

void apply_func(MFunc &func) {
  ZoneScopedN("Allocating Func");
  FMap<size_t, VRegType> reg_mapping;
  {
    ZoneScopedN("Args");
    gen_arg_mapping(func, reg_mapping);
    for (auto &bb : func.bbs) {
      for (auto &instr : bb.instrs) {
        replace_args(instr, reg_mapping);
      }
    }
    reg_mapping.clear();
  }

  const auto lifetimes = linear_lifetime(func);
  FMap<VRegType, LinearRange> lifeness;
  lifeness.reserve(32);
  constexpr VRegType regs[] = {
      VRegType::A,   VRegType::C,   VRegType::D,   VRegType::S,   VRegType::R8,
      VRegType::R9,  VRegType::R10, VRegType::R11, VRegType::R12, VRegType::R13,
      VRegType::R14, VRegType::R15, VRegType::B};

  {
    ZoneScopedN("Actual Alloc");

    for (auto [reg, lifetime] : lifetimes) {
      if (reg.info.is_pinned()) {
        lifeness.insert({reg.info.ty, lifetime});
      } else {
        bool found = false;
        for (auto avail_reg : regs) {
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
  FVec<utils::BitSet> used_regs;

  used_regs.resize(funcs.size(), utils::BitSet::empty(12));

  for (auto &func : funcs) {
    apply_func(func);
  }
}

} // namespace foptim::fmir
