#pragma once
#include "../func.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class Legalizer {
  u64 unique_reg_id;

  // helpers
  [[nodiscard]] MArgument get_reg(Type type);
  u32 move_arg_to_reg(MBB &bb, u32 indx, u8 arg_id, Type ty);
  u32 move_arg_to_pinned_reg(MBB &bb, u32 indx, u8 arg_id, Type ty,
                              VRegType vreg_ty);
  u32 move_fp_const_to_reg(MBB &bb, u32 indx, u8 arg_id, Type ty);
  u32 move_fp_const_to_grp(MBB &bb, u32 indx, u8 arg_id, Type ty);

  // legalizing
  void legalize_cmp(MBB &bb, u32 indx);
  void legalize_idiv(MBB &bb, u32 indx);
  void legalize_fadd(MBB &bb, u32 indx);
  void legalize_push(MBB &bb, u32 indx);
  void legalize_arg_setup(MBB &bb, u32 indx);
  void apply(MFunc &funcs);

public:
  void apply(FVec<MFunc> &funcs);
};

} // namespace foptim::fmir
