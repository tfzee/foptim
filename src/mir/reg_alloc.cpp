#include "reg_alloc.hpp"

#include "ir/value.hpp"
#include "mir/instr.hpp"
// #include "optim/analysis/cfg.hpp"
// #include "optim/analysis/dominators.hpp"
#include "mir/matcher.hpp"
#include "mir/matcher_helpers.hpp"

namespace foptim::fmir {

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
  const IndexedValue key{.v = value, .id = 0};
  if (auto it = mapping.find(key); it != mapping.end()) {
    return it->second;
  }
  auto new_reg = get_new_register(value_type(value));
  mapping.emplace(key, new_reg);
  return new_reg;
}

}  // namespace foptim::fmir
