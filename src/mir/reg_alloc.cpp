#include "reg_alloc.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/value.hpp"
#include "mir/instr.hpp"
// #include "optim/analysis/cfg.hpp"
// #include "optim/analysis/dominators.hpp"
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

VReg DumbRegAlloc::get_new_pinned_register(VRegInfo info) {
  utils::BitSet free_regs{vreg_num, true};
  bool wants_pinned = info.is_pinned();
  ASSERT(wants_pinned);
  vreg_num++;
  return VReg{vreg_num, info};
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

    if (lives.isLive(var, loc) ||
        !type_can_share_register(var.get_type(), ty)) {
      free_regs[reg.id - 1].set(false);
    }
  }
  for (auto reg : free_regs) {
    return VReg{reg + 1, info};
  }

  vreg_num++;
  return VReg{vreg_num, info};
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

VReg DumbRegAlloc::get_new_register(fir::IRLocation /*unused*/, Type /*unused*/,
                                    optim::LiveVariables & /*unused*/) {
  // TODO: imrpvoe
  //  utils::BitSet free_regs{vreg_num - 1, true};
  //  for (auto [var, reg] : mapping) {
  //    if (lives.isLive(var, loc) || !var.get_type()->eql(ty->get_raw())) {
  //      free_regs[reg.id - 1] = false;
  //    }
  //  }
  //  for (auto reg : free_regs) {
  //    return VReg{reg + 1};
  //  }

  vreg_num++;
  return VReg{vreg_num};
}

VReg DumbRegAlloc::get_new_register(fir::ValueR v,
                                    optim::LiveVariables &lives) {
  utils::BitSet free_regs{vreg_num, true};
  // utils::Debug << v << vreg_num << "\n";

  for (auto [var, reg] : mapping) {
    // utils::Debug << "    " << var << "   " << reg.id << "\n";

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

void DumbRegAlloc::alloc_func(fir::Function &func,
                              optim::LiveVariables &lives) {
  ZoneScopedN("Dumb Reg Alloc");
  reset();

  for (auto curr : func.get_bbs()) {
    // utils::Debug << "ALLOCING BB: " << (void *)curr.get_raw_ptr() << "\n";
    const u32 n_args = curr->get_args().size();
    for (u32 arg_id = 0; arg_id < n_args; arg_id++) {
      auto new_value = fir::ValueR(curr, arg_id);
      // entry block args we always need
      if (new_value.get_n_uses() > 0 || func.get_bbs()[0] == curr) {
        ASSERT(!mapping.contains(new_value));
        auto new_reg = get_new_register(new_value, lives);
        mapping.insert({new_value, new_reg});
      }
    }

    for (auto instr : curr->get_instrs()) {
      auto instr_valu = fir::ValueR(instr);
      ASSERT(!mapping.contains(instr_valu));
      if (instr->get_n_uses() > 0 && instr->has_result()) {
        // utils::Debug << "ALLOCING instr: " << (void *)instr.get_raw_ptr()
        //              << "\n";
        // ASSERT(!mapping.contains(instr_valu));
        auto new_reg = get_new_register(instr_valu, lives);
        mapping.insert({instr_valu, new_reg});
      }
      // else {
      //   utils::Debug << "NOT ALLOCING instr: " << (void *)instr.get_raw_ptr()
      //                << " " << instr->get_n_uses() << " " <<
      //                instr->has_result()
      //                << "\n";
      // }
    }
  }
  // dump();
  // std::abort();
}

VReg DumbRegAlloc::get_register(fir::ValueR value) {
  if (!mapping.contains(value)) {
    utils::Warn << " Dumb reg alloc couldnt find " << value << "\n";
    std::abort();
    // return get_new_register({});
  }
  return mapping.at(value);
}

void DumbRegAlloc::dump() {
  utils::Debug << "Unused regs: " << free_regs.size() << "\n";
  for (auto [v, a] : mapping) {
    utils::Debug << v << ": " << a << "\n";
  }
}
} // namespace foptim::fmir
