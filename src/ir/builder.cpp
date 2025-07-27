#include "builder.hpp"
#include "basic_block.hpp"
#include "function.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/value.hpp"
#include "utils/vec.hpp"
#include <algorithm>

namespace foptim::fir {

Builder::Builder(FunctionR func)
    : ctx(func->ctx), func(func), bb(BasicBlock::invalid()), indx(0) {}
Builder::Builder(BasicBlock bb)
    : ctx(bb->get_parent()->ctx), func(bb->get_parent()), bb(bb), indx(0) {}
Builder::Builder(Instr instr)
    : ctx(instr->get_parent()->get_parent()->ctx),
      func(instr->get_parent()->get_parent()), bb(instr->get_parent()),
      indx(0) {
  indx = std::find(bb->instructions.begin(), bb->instructions.end(), instr) -
         bb->instructions.begin();
}

BasicBlock Builder::append_bb() {
  auto res = BasicBlock(ctx->storage.basic_blocks.push_back({func}));
  res.verify_validness();
  func->append_bbr(res);
  if (!bb.is_valid()) {
    bb = res;
    indx = 0;
  }
  return res;
}

// void Builder::before(Instr instr) {
//   ctx = instr->get_parent()->get_parent()->ctx;
//   func = instr->get_parent()->get_parent()), bb(instr->get_parent();
//   indx = 0;
//   indx = std::find(bb->instructions.begin(), bb->instructions.end(), instr) -
//          bb->instructions.begin();
// }

void Builder::after(fir::Instr i) {
  indx = std::find(bb->instructions.begin(), bb->instructions.end(), i) -
         bb->instructions.begin() + 1;
  bb = i->get_parent();
  func = bb->get_parent();
}

void Builder::at_end(BasicBlock bbr) {
  bb = bbr;
  func = bbr->func;
  indx = bbr->instructions.size();
}

void Builder::at_start(BasicBlock bbr) {
  bb = bbr;
  func = bbr->func;
  indx = 0;
}

void Builder::at_penultimate(BasicBlock bbr) {
  bb = bbr;
  func = bbr->func;
  ASSERT(bbr->instructions.size() > 0);
  indx = bbr->instructions.size() - 1;
}

void Builder::check_bb_set() {
  ASSERT_M(bb.is_valid(), "Need to set basicblock insert location first with "
                          "funtions like at_end for exmaple");
}

ValueR Builder::build_call(ValueR func_ptr, TypeR func_type, TypeR ret_type,
                           std::span<ValueR> args) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_call(ret_type));
  instr.add_arg(func_ptr);
  instr->add_attrib("callee_type", func_type);
  for (auto arg : args) {
    instr.add_arg(arg);
  }
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_int_srem(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_smod(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_int_urem(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_umod(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_ctlz(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(
      InstrData::get_intrinsic(a.get_type(), IntrinsicSubType::CTLZ));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_va_start(ValueR a) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_intrinsic(
      ctx->get_void_type(), IntrinsicSubType::VA_start));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_va_end(ValueR a) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(
      InstrData::get_intrinsic(ctx->get_void_type(), IntrinsicSubType::VA_end));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_fabs(ValueR a) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(
      InstrData::get_intrinsic(a.get_type(), IntrinsicSubType::FAbs));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_abs(ValueR a) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(
      InstrData::get_intrinsic(a.get_type(), IntrinsicSubType::Abs));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_intrinsic(ValueR a, ValueR b, IntrinsicSubType type) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_intrinsic(a.get_type(), type));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}
ValueR Builder::build_binary_op(ValueR a, ValueR b,
                                BinaryInstrSubType sub_type) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_binary(a.get_type(), sub_type));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_unary_op(ValueR a, UnaryInstrSubType sub_type) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_unary(a.get_type(), sub_type));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_extract_value(ValueR stru, std::span<ValueR> indicies,
                                    TypeR out_ty) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_extract_value(out_ty));
  instr.add_arg(stru);
  for (auto ar : indicies) {
    instr.add_arg(ar);
  }
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_insert_value(ValueR stru, ValueR v,
                                   std::span<ValueR> indicies, TypeR out_ty) {

  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_insert_value(out_ty));
  instr.add_arg(stru);
  instr.add_arg(v);
  for (auto ar : indicies) {
    instr.add_arg(ar);
  }
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_conversion_op(ValueR a, TypeR res_type,
                                    ConversionSubType sub_type) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_conversion(res_type, sub_type));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_int_add(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_add(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_float_add(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_float_add(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_float_sub(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_float_sub(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_float_mul(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_float_mul(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_float_div(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_float_div(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_itrunc(ValueR a, TypeR ty) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_itrunc(ty));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_sext(ValueR a, TypeR ty) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_sext(ty));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_zext(ValueR a, TypeR ty) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_zext(ty));
  instr.add_arg(a);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_int_mul(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_mul(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_int_sub(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_sub(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_shl(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_shl(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_ashr(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_ashr(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_lshr(ValueR a, ValueR b) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_lshr(a.get_type()));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_int_cmp(ValueR a, ValueR b, ICmpInstrSubType ty) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(
      InstrData::get_int_cmp(ctx->get_int_type(1), ty));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_float_cmp(ValueR a, ValueR b, FCmpInstrSubType ty) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(
      InstrData::get_float_cmp(ctx->get_int_type(1), ty));
  instr.add_arg(a);
  instr.add_arg(b);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_alloca(ValueR size) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_alloca(ctx->get_ptr_type()));
  instr.add_arg(size);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_vbroadcast(ValueR v, TypeR type) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(
      InstrData::get_vector(type, VectorISubType::Broadcast));
  instr.add_arg(v);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

Instr Builder::build_branch(BasicBlock target_bb) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_branch(ctx));
  instr.add_bb(target_bb);
  bb.insert_instr(indx, instr);
  indx++;
  return instr;
}

Instr Builder::build_cond_branch(ValueR cond, BasicBlock true_bb,
                                 BasicBlock false_bb) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_cond_branch(ctx));
  instr.add_bb(true_bb);
  instr.add_bb(false_bb);
  instr.add_arg(cond);
  bb.insert_instr(indx, instr);
  indx++;
  return instr;
}

Instr Builder::build_switch(
    ValueR value,
    std::span<std::pair<fir::ConstantValueR, fir::BasicBlock>> targets,
    BasicBlock default_bb) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_switch(ctx));
  for (auto [v, b] : targets) {
    ASSERT(v->is_int());
    instr.add_bb(b);
    instr.add_arg(fir::ValueR{v});
  }
  if (default_bb.is_valid()) {
    instr.add_bb(default_bb);
  }
  instr.add_arg(value);
  bb.insert_instr(indx, instr);
  indx++;
  return instr;
}

ValueR Builder::build_load(TypeR type, ValueR ptr) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_load(type));
  instr.add_arg(ptr);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_select(TypeR type, ValueR cond, ValueR v1, ValueR v2) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_select(type));
  instr.add_arg(cond);
  instr.add_arg(v1);
  instr.add_arg(v2);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

ValueR Builder::build_store(ValueR ptr, ValueR value) {
  check_bb_set();
  Instr instr =
      ctx->storage.insert_instr(InstrData::get_store(value.get_type()));
  instr.add_arg(ptr);
  instr.add_arg(value);
  bb.insert_instr(indx, instr);
  indx++;
  return ValueR(instr);
}

Instr Builder::build_unreach() {
  check_bb_set();
  auto instr =
      ctx->storage.insert_instr(InstrData::get_unreach(ctx->get_void_type()));
  bb.insert_instr(indx, instr);
  indx++;
  return instr;
}

Instr Builder::build_return() {
  check_bb_set();
  auto instr =
      ctx->storage.insert_instr(InstrData::get_return(ctx->get_void_type()));
  bb.insert_instr(indx, instr);
  indx++;
  return instr;
}

Instr Builder::build_return(ValueR v) {
  check_bb_set();
  Instr instr = ctx->storage.insert_instr(InstrData::get_return(v.get_type()));
  instr.add_arg(v);
  bb.insert_instr(indx, instr);
  indx++;
  return instr;
}

BasicBlock Builder::insert_copy(BasicBlock bb, ContextData::V2VMap &subs) {
  BasicBlock new_bb = ctx->copy(bb, subs);
  new_bb->func = func;
  func->append_bbr(new_bb);
  return new_bb;
}

Instr Builder::move_instr(Instr instr) {
  check_bb_set();

  size_t instr_id = 0;
  auto parent = instr->parent;
  while (parent->instructions[instr_id] != instr) {
    instr_id++;
    ASSERT(instr_id < parent->instructions.size());
  }
  parent->instructions.erase(parent->instructions.begin() + (i64)instr_id);

  bb.insert_instr(indx, instr);
  indx++;
  return instr;
}

Instr Builder::insert_copy(Instr instr) {
  check_bb_set();
  Instr res = ctx->copy(instr);
  res->uses.clear();

  // TODO: this is inneficient and should be clened up
  auto args = res->args;
  res->args.clear();
  for (auto arg : args) {
    res.add_arg(arg);
  }
  auto bbs = res->bbs;
  res->bbs.clear();
  for (auto bb : bbs) {
    res.add_bb(bb.bb);
    // this works but is weird
    for (size_t arg_id = 0; arg_id < bb.args.size(); arg_id++) {
      res.replace_bb_arg(res->bbs.size() - 1, arg_id, bb.args[arg_id]);
    }
  }

  bb.insert_instr(indx, res);
  indx++;
  return res;
}

} // namespace foptim::fir
