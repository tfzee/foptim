#include "reg_alloc.hpp"
#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/set.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"

namespace foptim::fmir {
void replace_vargs(IRVec<MBB> &bbs, const TMap<u64, CReg> &reg_mapping) {
  for (auto &bb : bbs) {
    for (auto &instr : bb.instrs) {
      replace_vargs(instr, reg_mapping);
    }
  }
}

void replace_vargs(MInstr &instr, const TMap<u64, CReg> &reg_mapping) {
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
      if (!reg.is_concrete() && reg_mapping.contains(reg.virt_id())) {
        instr.args[i].reg.rty = VReg::RegType::Concrete;
        instr.args[i].reg.conc.creg = reg_mapping.at(reg.virt_id());
      }
      break;
    }
    case MArgument::ArgumentType::MemVRegVRegScale:
    case MArgument::ArgumentType::MemImmVRegVReg:
    case MArgument::ArgumentType::MemVRegVReg:
    case MArgument::ArgumentType::MemImmVRegVRegScale: {
      auto reg = instr.args[i].reg;
      auto indx = instr.args[i].indx;
      if (!reg.is_concrete() && reg_mapping.contains(reg.virt_id())) {
        instr.args[i].reg.rty = VReg::RegType::Concrete;
        instr.args[i].reg.conc.creg = reg_mapping.at(reg.virt_id());
      }
      if (!indx.is_concrete() && reg_mapping.contains(indx.virt_id())) {
        instr.args[i].indx.rty = VReg::RegType::Concrete;
        instr.args[i].indx.conc.creg = reg_mapping.at(indx.virt_id());
      }
      break;
    }
    case MArgument::ArgumentType::MemImmVRegScale: {
      auto indx = instr.args[i].indx;
      if (!indx.is_concrete() && reg_mapping.contains(indx.virt_id())) {
        instr.args[i].indx.rty = VReg::RegType::Concrete;
        instr.args[i].indx.conc.creg = reg_mapping.at(indx.virt_id());
      }
      break;
    }
    }
  }
}

bool reg_is_legal(const VReg &reg, CReg avail_reg) {
  ASSERT(!reg.is_concrete());
  ASSERT((u8)CReg::N_REGS == 33);

  if (reg.is_vec_reg()) {
    return avail_reg == CReg::mm0 || avail_reg == CReg::mm1 ||
           avail_reg == CReg::mm2 || avail_reg == CReg::mm3 ||
           avail_reg == CReg::mm4 || avail_reg == CReg::mm5 ||
           avail_reg == CReg::mm6 || avail_reg == CReg::mm7 ||
           avail_reg == CReg::mm8 || avail_reg == CReg::mm9 ||
           avail_reg == CReg::mm10 || avail_reg == CReg::mm11 ||
           avail_reg == CReg::mm12 || avail_reg == CReg::mm13 ||
           avail_reg == CReg::mm14 || avail_reg == CReg::mm15;
  }
  return avail_reg == CReg::A || avail_reg == CReg::B || avail_reg == CReg::C ||
         avail_reg == CReg::D || avail_reg == CReg::DI ||
         avail_reg == CReg::SI || avail_reg == CReg::SP ||
         avail_reg == CReg::BP || avail_reg == CReg::R8 ||
         avail_reg == CReg::R9 || avail_reg == CReg::R10 ||
         avail_reg == CReg::R11 || avail_reg == CReg::R12 ||
         avail_reg == CReg::R13 || avail_reg == CReg::R14 ||
         avail_reg == CReg::R15;
}

constexpr size_t N_REGS_SELECTABLE = 30;
static_assert((size_t)CReg::N_REGS - 3 == N_REGS_SELECTABLE);

constexpr void get_reg_order(MFunc &func, CReg *regs) {
  static constexpr CReg leaf_optimized_regs[N_REGS_SELECTABLE] = {
      CReg::A,    CReg::D,    CReg::C,    CReg::DI,   CReg::SI,   CReg::R8,
      CReg::R9,   CReg::R10,  CReg::R11,  CReg::mm0,  CReg::mm1,  CReg::mm2,
      CReg::mm3,  CReg::mm4,  CReg::mm5,  CReg::mm6,  CReg::mm7,  CReg::R12,
      CReg::R13,  CReg::R14,  CReg::R15,  CReg::B,    CReg::mm8,  CReg::mm9,
      CReg::mm10, CReg::mm11, CReg::mm12, CReg::mm13, CReg::mm14, CReg::mm15};
  static constexpr CReg basic_regs[N_REGS_SELECTABLE] = {
      CReg::B,    CReg::R12,  CReg::R13,  CReg::R14,  CReg::R15,  CReg::mm8,
      CReg::mm9,  CReg::mm10, CReg::mm11, CReg::mm12, CReg::mm13, CReg::mm14,
      CReg::mm15, CReg::A,    CReg::D,    CReg::C,    CReg::DI,   CReg::SI,
      CReg::R8,   CReg::R9,   CReg::R10,  CReg::R11,  CReg::mm0,  CReg::mm1,
      CReg::mm2,  CReg::mm3,  CReg::mm4,  CReg::mm5,  CReg::mm6,  CReg::mm7};

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

  const CReg *selected = basic_regs;
  if (is_leaf) {
    selected = leaf_optimized_regs;
  }

  for (size_t i = 0; i < N_REGS_SELECTABLE; i++) {
    regs[i] = selected[i];
  }
}

void spill_one(MFunc &func, TVec<VReg>& spillers, const TMap<VReg, TSet<size_t>> &reg_coll) {
  fmt::println("========================\n{}", func);
  fmt::println("========================\n{}", spillers);
  TODO("spill it ?");
  ASSERT(false);
  (void)reg_coll;
  // auto biggest =
}

void apply_func(MFunc &func) {
  ZoneScopedN("Allocating Func");
  TMap<CReg, TSet<size_t>> lifeness;
  lifeness.reserve(32);
  CReg regs[N_REGS_SELECTABLE];
  TMap<size_t, CReg> reg_mapping;

  // TVec<std::pair<VReg, const TSet<size_t> *>> iter_lifetimes;
  // for (const auto &[r, l] : lifetimes) {
  //   iter_lifetimes.emplace_back(r, &l);
  // }
  // std::sort(iter_lifetimes.begin(), iter_lifetimes.end(),
  //           [](const auto &a, const auto &b) {
  //             return a.second->size() < b.second->size();
  //           });
  get_reg_order(func, regs);

  {
    ZoneScopedN("Actual Alloc");
    TVec<VReg> spillers;
    while (true) {
      reg_mapping.clear();
      lifeness.clear();
      spillers.clear();
      const auto lifetimes = reg_coll(func);

      for (const auto &[reg, colls] : lifetimes) {
        // fmt::print("{}: ", reg);
        // for (auto coll : colls) {
        //   fmt::print("{}:{}, ", uid_to_reg(coll), coll);
        // }
        // fmt::println("");
        if (reg.is_concrete()) {
          lifeness.insert({reg.c_reg(), colls});
        }
      }


      bool successfully_allocated = true;
      for (const auto &[reg, colls] : lifetimes) {
        if (reg.is_concrete()) {
          continue;
        }
        bool found = false;
        auto reg_id = reg_to_uid(reg);
        for (auto avail_reg : regs) {
          if (!reg_is_legal(reg, avail_reg)) {
            continue;
          }

          if (!lifeness.contains(avail_reg)) {
            lifeness.insert({avail_reg, colls});
            reg_mapping.insert({reg.virt_id(), avail_reg});
            found = true;
            break;
          }
          if (!lifeness.at(avail_reg).contains(reg_id)) {
            lifeness.at(avail_reg).insert(colls.begin(), colls.end());
            reg_mapping.insert({reg.virt_id(), avail_reg});
            found = true;
            break;
          }
        }
        if (!found) {
          spillers.push_back(reg);
          successfully_allocated = false;
          // fmt::println("========================\n{}", func);
          // fmt::println("{} IN FUNC: {}", reg, func.name.c_str());
          // fmt::println("{} Size:: {} IS vec: {}", reg, reg.ty,
          // reg.is_vec_reg());
          // TODO("spill it ?");
          // ASSERT(false);
        }
      }
      if (successfully_allocated) {
        break;
      }
      // if we didnt find one spill one
      spill_one(func,spillers, lifetimes);
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
