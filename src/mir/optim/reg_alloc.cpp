#include "reg_alloc.hpp"
#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/set.hpp"
#include "utils/todo.hpp"

namespace foptim::fmir {
void replace_vargs(IRVec<MBB> &bbs, const TMap<size_t, VRegType> &reg_mapping) {
  for (auto &bb : bbs) {
    for (auto &instr : bb.instrs) {
      replace_vargs(instr,reg_mapping);
    }
  }
}

void replace_vargs(MInstr &instr, const TMap<size_t, VRegType> &reg_mapping) {
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
    UNREACH();
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

  const auto lifetimes = linear_lifetime(func);
  get_reg_order(func, regs);

  {
    ZoneScopedN("Actual Alloc");
    for (const auto &[reg, lifetime] : lifetimes) {
      if (reg.info.is_pinned()) {
        lifeness.insert({reg.info.ty, lifetime});
      }
    }
    // utils::Debug << "Allocating\n";

    for (const auto &[reg, lifetime] : lifetimes) {
      if (reg.info.is_pinned()) {
        continue;
      }
      // utils::Debug << " Trying to allocate " << reg
      //              << " UID:" << reg_to_uid(reg) << "\n";
      // utils::Debug << " WithLifetime: ";
      // for (auto life : lifetime.ranges) {
      //   life.dump();
      //   utils::Debug << " ";
      // }
      // utils::Debug << "\n";
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
        utils::Debug << reg << " IN FUNC:" << func.name.c_str() << "\n";
        utils::Debug << reg << " Size:" << reg.info.reg_size
                     << " Is FP:" << (reg.info.reg_class == VRegClass::Float)
                     << "\n";
        TODO("spill it ?");
        ASSERT(false);
      }
      // utils::Debug << "    Found reg:" << reg_mapping[reg.id] << "\n";
    }
  }

  for (auto &bb : func.bbs) {
    for (auto &instr : bb.instrs) {
      replace_vargs(instr, reg_mapping);
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
