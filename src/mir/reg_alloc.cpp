#include "reg_alloc.hpp"
#include "ir/IRLocation.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/value.hpp"
#include "mir/instr.hpp"
// #include "optim/analysis/cfg.hpp"
// #include "optim/analysis/dominators.hpp"
#include "mir/matcher.hpp"
#include "optim/analysis/live_variables.hpp"
#include "utils/bitset.hpp"
#include "utils/logging.hpp"

namespace foptim::fmir {
DumbRegAlloc::DumbRegAlloc() { reset(); }

void DumbRegAlloc::reset() {
  free_regs.clear();
  mapping.clear();
  vreg_num = 1;
}

// VReg DumbRegAlloc::create_new_register(fir::ValueR) {
//   vreg_num++;
//   return VReg{vreg_num};
// }

bool type_can_share_register(fir::TypeR a, fir::TypeR b) {
  if ((a->is_int() || a->is_ptr()) && (b->is_int() || b->is_ptr())) {
    return true;
  }
  if (a->is_float() && b->is_float()) {
    return true;
  }
  return a->eql(*b.get_raw_ptr());
}

VReg DumbRegAlloc::get_new_pinned_register(fir::IRLocation loc, VRegInfo info) {
  utils::BitSet free_regs{vreg_num, true};
  bool wants_pinned = info.is_pinned();
  ASSERT(wants_pinned);
  vreg_num++;
  auto res = VReg{vreg_num, info};
  additional_alives[loc].push_back(res);
  return res;
}

VReg DumbRegAlloc::get_new_register(fir::IRLocation loc, fir::TypeR ty,
                                    VRegInfo info,
                                    optim::LiveVariables &lives) {
  utils::BitSet free_regs{vreg_num, true};
  bool wants_pinned = info.is_pinned();

  for (auto [var, reg] : mapping) {
    if (wants_pinned && reg.info == info) {
      return reg;
    }

    auto &addit = additional_alives[loc];
    bool is_already_localy_used = std::ranges::find(addit, reg) != addit.cend();

    if (lives.isLive(var, loc) ||
        !type_can_share_register(var.get_type(), ty) ||
        is_already_localy_used) {
      free_regs[reg.id - 1].set(false);
    }
  }

  for (auto reg : free_regs) {
    auto res = VReg{reg + 1, info};
    additional_alives[loc].push_back(res);
    return res;
  }

  vreg_num++;
  auto res = VReg{vreg_num, info};
  additional_alives[loc].push_back(res);
  return res;
}

VReg DumbRegAlloc::get_new_register(fir::IRLocation loc, fir::TypeR ty,
                                    optim::LiveVariables &lives) {
  if (ty->is_float()) {
    return get_new_register(loc, ty, VRegInfo{VRegClass::Float}, lives);
  }
  return get_new_register(loc, ty, VRegInfo{VRegClass::GeneralPurpose}, lives);
}

VReg DumbRegAlloc::get_new_register(VRegInfo info) {
  vreg_num++;
  return VReg{vreg_num, info};
}

VReg DumbRegAlloc::get_new_register(fir::ValueR v,
                                    optim::LiveVariables &lives) {
  utils::BitSet free_regs{vreg_num, true};

  for (auto [var, reg] : mapping) {
    if (lives.collide(var, v) ||
        !var.get_type()->eql(v.get_type()->get_raw())) {
      free_regs[reg.id - 1].set(false);
    }
  }

  bool is_float = v.get_type()->is_float();
  for (auto reg : free_regs) {
    return VReg{reg + 1, (u8)v.get_type()->get_size(),
                is_float ? VRegClass::Float : VRegClass::GeneralPurpose};
  }
  vreg_num++;
  return VReg{vreg_num, (u8)v.get_type()->get_size(),
              is_float ? VRegClass::Float : VRegClass::GeneralPurpose};
}


VReg DumbRegAlloc::get_register(fir::ValueR value) {
  if (!mapping.contains(value)) {
    // if (value.get_n_uses() == 1 && value.is_instr() && value.as_instr().) {
    auto new_reg = get_new_register(VRegInfo{convert_type(value.get_type())});
    mapping.insert({value, new_reg});
  }
  return mapping.at(value);
}

void DumbRegAlloc::dump() {
  fmt::println("Unused regs: {}", free_regs.size());
  for (auto [v, a] : mapping) {
    TODO("reimpl");
    // fmt::println("{}: {}", v, a);
  }
}
} // namespace foptim::fmir
