#include "reg_alloc.hpp"

#include "ir/value.hpp"
#include "mir/instr.hpp"
// #include "optim/analysis/cfg.hpp"
// #include "optim/analysis/dominators.hpp"
#include "mir/matcher.hpp"

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
  if (!mapping.contains({value, 0})) {
    auto new_reg = get_new_register(convert_type(value.get_type()));
    mapping.insert({{.v = value, .id = 0}, new_reg});
  }
  return mapping.at({value, 0});
}

VReg DumbRegAlloc::get_struct_register(fir::ValueR value, fir::TypeR t,
                                       u32 id) {
  if (!mapping.contains({value, id})) {
    auto new_reg = get_new_register(convert_type(t));
    mapping.insert({{.v = value, .id = id}, new_reg});
  }
  return mapping.at({value, id});
}

}  // namespace foptim::fmir
