#pragma once
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class Legalizer {
  u64 unique_reg_id;

  // helpers
  [[nodiscard]] MArgument get_reg(Type type);
  u32 move_arg_to_reg(MBB &bb, u32 indx, u8 arg_id, Type ty);
  u32 move_arg_to_pinned_reg(MBB &bb, u32 indx, u8 arg_id, Type ty,
                             CReg vreg_ty);
  u32 move_fp_const_to_reg(MBB &bb, u32 indx, u8 arg_id, Type ty);
  u32 move_fp_const_to_grp(MBB &bb, u32 indx, u8 arg_id, Type ty);

  // legalizing
  bool legalize_punpckl(MBB &bb, u32 indx);
  bool legalize_sqrt(MBB &bb, u32 indx);
  bool legalize_icmp(MBB &bb, u32 indx);
  bool legalize_arith_op(MBB &bb, u32 indx);
  bool legalize_fcmp(MBB &bb, u32 indx);
  bool legalize_idiv(MBB &bb, u32 indx);
  bool legalize_conversion(MBB &bb, u32 indx);
  bool legalize_floating_binary_ops(MBB &bb, u32 indx);
  bool legalize_push(MBB &bb, u32 indx);
  bool legalize_arg_setup(MBB &bb, u32 indx);
  bool legalize_move(MBB &bb, u32 indx);
  bool legalize_cmove(MBB &bb, u32 indx);
  bool legalize_cmoveXX(MBB &bb, u32 indx);
  // bool legalize_si2fl(MBB &bb, u32 indx);
  // bool legalize_sub(MBB &bb, u32 indx);
  void apply_impl(MFunc &funcs);

 public:
  void apply(MFunc &funcs);
  void apply(FVec<MFunc> &funcs);
};

}  // namespace foptim::fmir
