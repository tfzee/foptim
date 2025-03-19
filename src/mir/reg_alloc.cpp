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

// bool type_can_share_register(fir::TypeR a, fir::TypeR b) {
//   if ((a->is_int() || a->is_ptr()) && (b->is_int() || b->is_ptr())) {
//     return true;
//   }
//   if (a->is_float() && b->is_float()) {
//     return true;
//   }
//   return a->eql(*b.get_raw_ptr());
// }

VReg DumbRegAlloc::get_new_register(Type type) {
  vreg_num++;
  return VReg{vreg_num, type};
}
VReg DumbRegAlloc::get_register(fir::ValueR value) {
  if (!mapping.contains(value)) {
    auto new_reg = get_new_register(convert_type(value.get_type()));
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
