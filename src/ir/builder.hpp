#pragma once
#include "basic_block.hpp"
#include "context.hpp"
#include "function.hpp"
#include "instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/value.hpp"
#include "utils/logging.hpp"
#include "utils/vec.hpp"
#include <algorithm>
#include <span>

namespace foptim::fir {
class Builder {
  ContextData *ctx;
  FunctionR func;
  BasicBlock bb;
  size_t indx;

public:
  Builder(FunctionR func)
      : ctx(func->ctx), func(func), bb(BasicBlock::invalid()), indx(0) {}
  Builder(BasicBlock bb)
      : ctx(bb->get_parent()->ctx), func(bb->get_parent()), bb(bb), indx(0) {}
  Builder(Instr instr)
      : ctx(instr->get_parent()->get_parent()->ctx),
        func(instr->get_parent()->get_parent()), bb(instr->get_parent()),
        indx(0) {
    indx = std::find(bb->instructions.begin(), bb->instructions.end(), instr) -
           bb->instructions.begin();
  }

  /*
    Sets the current insert location to the start of the new Basic Block
  */
  BasicBlock append_bb() {
    auto res = BasicBlock(ctx->storage.basic_blocks.push_back({func}));
    res.verify_validness();
    func->append_bbr(res);
    if (!bb.is_valid()) {
      bb = res;
      indx = 0;
    }
    return res;
  }

  void at_end(BasicBlock bbr) {
    bb = bbr;
    func = bbr->func;
    indx = bbr->instructions.size();
  }

  void at_penultimate(BasicBlock bbr) {
    bb = bbr;
    func = bbr->func;
    ASSERT(bbr->instructions.size() > 0);
    indx = bbr->instructions.size() - 1;
  }

  void check_bb_set() {
    ASSERT_M(bb.is_valid(), "Need to set basicblock insert location first with "
                            "funtions like at_end for exmaple");
  }

  ValueR build_direct_call(std::string name, TypeR func_type, TypeR ret_type,
                           std::span<ValueR> args) {
    check_bb_set();
    Instr instr =
        ctx->storage.insert_instr(InstrData::get_direct_call(ret_type));
    instr->add_attrib("callee", name);
    instr->add_attrib("callee_type", func_type);
    for (auto arg : args) {
      instr.add_arg(arg);
    }
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  // ValueR build_gep(ValueR ptr, TypeR res_type,
  //                          FVec<ValueR> args) {
  //   check_bb_set();
  //   Instr instr = ctx->storage.insert_instr(InstrData::get_gep(ret_type));
  //   instr.add_arg(ptr);
  //   for (auto arg : args) {
  //     instr.add_arg(arg);
  //   }
  //   bb.insert_instr(indx, instr);
  //   indx++;
  //   return ValueR(instr);
  // }

  ValueR build_int_srem(ValueR a, ValueR b) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(InstrData::get_smod(a.get_type()));
    instr.add_arg(a);
    instr.add_arg(b);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  ValueR build_int_add(ValueR a, ValueR b) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(InstrData::get_add(a.get_type()));
    instr.add_arg(a);
    instr.add_arg(b);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  ValueR build_sext(ValueR a, TypeR ty) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(InstrData::get_sext(ty));
    instr.add_arg(a);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  ValueR build_zext(ValueR a, TypeR ty) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(InstrData::get_zext(ty));
    instr.add_arg(a);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  ValueR build_int_mul(ValueR a, ValueR b) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(InstrData::get_mul(a.get_type()));
    instr.add_arg(a);
    instr.add_arg(b);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  ValueR build_int_sub(ValueR a, ValueR b) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(InstrData::get_sub(a.get_type()));
    instr.add_arg(a);
    instr.add_arg(b);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  ValueR build_int_cmp(ValueR a, ValueR b, ICmpInstrSubType ty) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(
        InstrData::get_int_cmp(ctx->get_int_type(8), ty));
    instr.add_arg(a);
    instr.add_arg(b);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  ValueR build_alloca(ValueR size) {
    check_bb_set();
    Instr instr =
        ctx->storage.insert_instr(InstrData::get_alloca(ctx->get_ptr_type()));
    instr.add_arg(size);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  Instr build_branch(BasicBlock target_bb) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(InstrData::get_branch(ctx));
    instr.add_bb(target_bb);
    bb.insert_instr(indx, instr);
    indx++;
    return instr;
  }

  Instr build_cond_branch(ValueR cond, BasicBlock true_bb,
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

  ValueR build_load(TypeR type, ValueR ptr) {
    check_bb_set();
    Instr instr = ctx->storage.insert_instr(InstrData::get_load(type));
    instr.add_arg(ptr);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  ValueR build_store(ValueR ptr, ValueR value) {
    check_bb_set();
    Instr instr =
        ctx->storage.insert_instr(InstrData::get_store(value.get_type()));
    instr.add_arg(ptr);
    instr.add_arg(value);
    bb.insert_instr(indx, instr);
    indx++;
    return ValueR(instr);
  }

  void build_return() {
    check_bb_set();
    bb.insert_instr(indx, ctx->storage.insert_instr(
                              InstrData::get_return(ctx->get_void_type())));
    indx++;
  }

  void build_return(ValueR v) {
    check_bb_set();
    Instr instr =
        ctx->storage.insert_instr(InstrData::get_return(v.get_type()));
    instr.add_arg(v);
    bb.insert_instr(indx, instr);
    indx++;
  }

  Instr insert_copy(Instr instr) {
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
};

} // namespace foptim::fir
