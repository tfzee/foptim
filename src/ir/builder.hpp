#pragma once
#include <span>

#include "context.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::fir {
struct ContextData;
class TypeR;
class ValueR;
enum class BinaryInstrSubType : u32;
enum class ICmpInstrSubType : u32;
enum class UnaryInstrSubType : u32;

class Builder {
  ContextData *ctx;
  FunctionR func;
  BasicBlock bb;
  size_t indx;

 public:
  Builder(FunctionR func);
  Builder(BasicBlock bb);
  Builder(Instr instr);

  BasicBlock get_curr_bb() { return bb; }
  /*
    Sets the current insert location to the start of the new Basic Block if no
    location is set
  */
  BasicBlock append_bb();

  void after(Instr instr);
  void at_end(BasicBlock bbr);
  void at_start(BasicBlock bbr);
  void at_penultimate(BasicBlock bbr);
  void check_bb_set();

  ValueR build_call(ValueR func_ptr, TypeR func_type, TypeR ret_type,
                    std::span<ValueR> args);
  ValueR build_int_srem(ValueR a, ValueR b);
  ValueR build_int_urem(ValueR a, ValueR b);
  ValueR build_binary_op(ValueR a, ValueR b, BinaryInstrSubType sub_type);
  ValueR build_unary_op(ValueR a, UnaryInstrSubType sub_type);
  ValueR build_conversion_op(ValueR a, TypeR res_type,
                             ConversionSubType sub_type);
  ValueR build_vector_op(ValueR val, TypeR res_type, VectorISubType sub_type);
  ValueR build_vector_op(ValueR v1, ValueR v2, TypeR res_type,
                         VectorISubType sub_type);
  ValueR build_vector_op(ValueR v1, ValueR v2, ValueR v3, TypeR res_type,
                         VectorISubType sub_type);
  ValueR build_atomic_rmw(ValueR ptr, ValueR val, AtomicRMWSubType sub_type, Ordering ordering);
  ValueR build_fence(Ordering ordering);
  ValueR build_ctlz(ValueR a, ValueR b);
  ValueR build_va_start(ValueR a);
  ValueR build_va_end(ValueR a);
  ValueR build_fabs(ValueR a);
  ValueR build_abs(ValueR a);
  ValueR build_intrinsic(ValueR a, IntrinsicSubType type,
                         TypeR opt_type = TypeR{TypeR::invalid()});
  ValueR build_intrinsic(ValueR a, ValueR b, IntrinsicSubType type);
  ValueR build_int_add(ValueR a, ValueR b, bool nuw = false, bool nsw = false);
  ValueR build_float_add(ValueR a, ValueR b);
  ValueR build_float_sub(ValueR a, ValueR b);
  ValueR build_float_mul(ValueR a, ValueR b);
  ValueR build_float_div(ValueR a, ValueR b);
  ValueR build_itrunc(ValueR a, TypeR ty);
  ValueR build_extract_value(ValueR stru, std::span<ValueR> indicies,
                             TypeR out_ty);
  ValueR build_insert_value(ValueR stru, ValueR v, std::span<ValueR> indicies,
                            TypeR out_ty);
  ValueR build_sext(ValueR a, TypeR ty);
  ValueR build_zext(ValueR a, TypeR ty);
  ValueR build_int_mul(ValueR a, ValueR b, bool nuw = false, bool nsw = false);
  ValueR build_int_sub(ValueR a, ValueR b);
  ValueR build_shl(ValueR a, ValueR b);
  ValueR build_ashr(ValueR a, ValueR b);
  ValueR build_lshr(ValueR a, ValueR b);
  ValueR build_int_cmp(ValueR a, ValueR b, ICmpInstrSubType ty);
  ValueR build_float_cmp(ValueR a, ValueR b, FCmpInstrSubType ty);
  ValueR build_alloca(ValueR size);
  ValueR build_vbroadcast(ValueR val, TypeR type);
  Instr build_branch(BasicBlock target_bb);
  Instr build_cond_branch(ValueR cond, BasicBlock true_bb, BasicBlock false_bb);
  Instr build_switch(
      ValueR value,
      std::span<std::pair<fir::ConstantValueR, fir::BasicBlock>> targets,
      BasicBlock default_bb);
  ValueR build_select(TypeR type, ValueR cond, ValueR v1, ValueR v2);
  ValueR build_load(TypeR type, ValueR ptr, bool is_atomic, bool is_volatile);
  ValueR build_store(ValueR ptr, ValueR value, bool is_atomic, bool is_volatile);
  Instr build_unreach();
  Instr build_return();
  Instr build_return(ValueR v);

  BasicBlock insert_copy(BasicBlock bb, ContextData::V2VMap &subs);
  Instr insert_copy(Instr instr);
  Instr move_instr(Instr instr);
};

}  // namespace foptim::fir
