#include <fmt/core.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <bit>

#include "inst_simplify.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/attributer/KnownBits.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "utils/helpers.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

namespace {
using WorkList = TVec<InstSimplify::WorkItem>;

void swap_args(fir::Instr instr, u32 a1, u32 a2) {
  using namespace foptim::fir;
  auto v1 = instr->args[a1];
  auto v2 = instr->args[a2];
  instr.replace_arg(a1, v2);
  instr.replace_arg(a2, v1);
}

void swap_args_fcmp(fir::Instr instr) {
  ASSERT(instr->is(fir::InstrType::FCmp))
  switch ((fir::FCmpInstrSubType)instr->subtype) {
    case fir::FCmpInstrSubType::INVALID:
    case fir::FCmpInstrSubType::AlwFalse:
    case fir::FCmpInstrSubType::OEQ:
    case fir::FCmpInstrSubType::UEQ:
    case fir::FCmpInstrSubType::ONE:
    case fir::FCmpInstrSubType::UNE:
    case fir::FCmpInstrSubType::AlwTrue:
      swap_args(instr, 0, 1);
      break;
    case fir::FCmpInstrSubType::OGT:
    case fir::FCmpInstrSubType::OGE:
    case fir::FCmpInstrSubType::OLT:
    case fir::FCmpInstrSubType::OLE:
    case fir::FCmpInstrSubType::ORD:
    case fir::FCmpInstrSubType::UNO:
    case fir::FCmpInstrSubType::UGT:
    case fir::FCmpInstrSubType::UGE:
    case fir::FCmpInstrSubType::ULT:
    case fir::FCmpInstrSubType::ULE:
    case fir::FCmpInstrSubType::IsNaN:
      break;
  }
}

void swap_args_icmp(fir::Instr instr) {
  ASSERT(instr->is(fir::InstrType::ICmp))
  switch ((fir::ICmpInstrSubType)instr->subtype) {
    case fir::ICmpInstrSubType::NE:
    case fir::ICmpInstrSubType::EQ:
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::SLT:
      instr->subtype = (u32)fir::ICmpInstrSubType::SGT;
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::ULT:
      instr->subtype = (u32)fir::ICmpInstrSubType::UGT;
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::SGT:
      instr->subtype = (u32)fir::ICmpInstrSubType::SLT;
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::UGT:
      instr->subtype = (u32)fir::ICmpInstrSubType::ULT;
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::UGE:
      instr->subtype = (u32)fir::ICmpInstrSubType::ULE;
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::ULE:
      instr->subtype = (u32)fir::ICmpInstrSubType::UGE;
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::SGE:
      instr->subtype = (u32)fir::ICmpInstrSubType::SLE;
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::SLE:
      instr->subtype = (u32)fir::ICmpInstrSubType::SGE;
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::AddOverflow:
    case fir::ICmpInstrSubType::MulOverflow:
      // TODO
      return;
    case fir::ICmpInstrSubType::INVALID:
      UNREACH();
  }
}

void push_all_uses(WorkList &worklist, fir::Instr instr) {
  for (auto &use : instr->uses) {
    worklist.emplace_back(use.user, use.user->parent);
  }
}
bool simplify_reduction(fir::Instr instr, fir::BasicBlock /*bb*/,
                        fir::Context &ctx, WorkList &worklist,
                        AttributerManager &man) {
  // detect reductions
  //  and try to simplify it
  //  for example merging constants on different leaves of the reduction if
  //  possible and merging duplicate values in the reduction
  using namespace foptim::fir;
  (void)man;

  auto instr_ty = instr->instr_type;
  auto instr_sty = instr->subtype;
  i128 neutral_val = 0;
  if (!instr->is(fir::InstrType::BinaryInstr) ||
      instr->subtype != (u32)BinaryInstrSubType::IntAdd) {
    // TODO: support stuff like min/max reductions
    return false;
  }

  TVec<fir::ValueR> red_args;
  TVec<fir::ValueR> red_worklist{fir::ValueR{instr}};
  while (!red_worklist.empty()) {
    auto c = red_worklist.back();
    red_worklist.pop_back();
    if (c.is_instr()) {
      auto c_in = c.as_instr();
      // fmt::print("   P {}", c_in);
      if (c_in->instr_type == instr_ty && c_in->subtype == instr_sty) {
        for (auto arg : c_in->args) {
          red_worklist.push_back(arg);
        }
      } else {
        red_args.push_back(c);
      }
    } else {
      red_args.push_back(c);
    }
  }
  if (red_args.size() <= 2) {
    return false;
  }
  i128 constval = neutral_val;
  std::ranges::sort(red_args, [](const auto &a, const auto &b) {
    if (a.is_constant()) {
      if (b.is_constant()) {
        return a.as_constant().get_raw_ptr() < b.as_constant().get_raw_ptr();
      }
      return true;
    }
    if (a.is_instr() && b.is_instr()) {
      return a.as_instr().get_raw_ptr() < b.as_instr().get_raw_ptr();
    }
    if (a.is_bb_arg() && b.is_bb_arg()) {
      return a.as_bb_arg().get_raw_ptr() < b.as_bb_arg().get_raw_ptr();
    }
    return false;
  });
  {
    u32 n_const = 0;
    size_t max_group_size = 0;
    for (size_t i = 0; i < red_args.size(); i++) {
      if (red_args[i].is_constant() && red_args[i].as_constant()->is_int()) {
        n_const++;
        constval += red_args[i].as_constant()->as_int();
      } else {
        size_t endgroup = i + 1;
        for (size_t i2 = i + 1; i2 < red_args.size(); i2++) {
          if (red_args[i] != red_args[i2]) {
            endgroup = i2;
            break;
          }
        }
        max_group_size = std::max(max_group_size, endgroup - i);
      }
    }
    if (max_group_size <= 2 || n_const <= 2) {
      return false;
    }
  }

  fir::Builder b{instr};
  auto out_bitwidth = instr->get_type()->get_bitwidth();
  auto red_v = fir::ValueR{ctx->get_constant_int(constval, out_bitwidth)};

  for (size_t i = 0; i < red_args.size(); i++) {
    if (red_args[i].is_constant() && red_args[i].as_constant()->is_int()) {
      continue;
    }
    size_t endgroup = i + 1;
    for (size_t i2 = i + 1; i2 < red_args.size(); i2++) {
      if (red_args[i] != red_args[i2]) {
        endgroup = i2;
        break;
      }
    }
    if (i + 1 != endgroup) {
      auto group_size = endgroup - i;
      auto mul_res = b.build_int_mul(
          red_args[i],
          fir::ValueR{ctx->get_constant_int(group_size, out_bitwidth)});
      worklist.push_back(
          {mul_res.as_instr(), mul_res.as_instr()->get_parent()});
      red_v = b.build_int_add(red_v, mul_res);
      worklist.push_back({red_v.as_instr(), red_v.as_instr()->get_parent()});
      i = endgroup - 1;
    } else {
      red_v = b.build_int_add(red_v, red_args[i]);
      worklist.push_back({red_v.as_instr(), red_v.as_instr()->get_parent()});
    }
  }
  worklist.push_back({red_v.as_instr(), red_v.as_instr()->get_parent()});
  push_all_uses(worklist, instr);
  instr->replace_all_uses(red_v);
  instr.destroy();
  // for (auto arg : red_args) {
  //   fmt::println("   {}", arg);
  // }
  // fmt::println("Const {}", constval);
  // fmt::println("CONVERTED");
  return true;
}

void simplify_binary(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                     WorkList &worklist, AttributerManager &man) {
  using namespace foptim::fir;
  // since both being constant would be handleded by constant folding we
  // just asume theres one and normalzie by putting it into the secodn arg
  {
    auto swap_const_back = instr->args[0].is_constant() &&
                           (!instr->args[0].as_constant()->is_global() &&
                            !instr->args[0].as_constant()->is_func());
    if (instr->is_commutative() && swap_const_back) {
      swap_args(instr, 0, 1);
    }
  }

  // (x &|^<<>> constant1) &|^<<>> constant2
  // x &|^<<>> (constant1 &|^<<>> constant2)
  // ONLY if the operator matches
  if ((instr->subtype == (u32)BinaryInstrSubType::And ||
       instr->subtype == (u32)BinaryInstrSubType::Or ||
       instr->subtype == (u32)BinaryInstrSubType::Xor ||
       instr->subtype == (u32)BinaryInstrSubType::Shl ||
       instr->subtype == (u32)BinaryInstrSubType::Shr ||
       instr->subtype == (u32)BinaryInstrSubType::IntMul ||
       instr->subtype == (u32)BinaryInstrSubType::IntSDiv ||
       instr->subtype == (u32)BinaryInstrSubType::IntUDiv) &&
      instr->args[1].is_constant() && instr->args[0].is_instr()) {
    auto inner_add_sub = instr->args[0].as_instr();
    if (inner_add_sub->is(InstrType::BinaryInstr) &&
        inner_add_sub->subtype == instr->subtype &&
        inner_add_sub->args[1].is_constant()) {
      fir::Builder b(instr);
      auto x = inner_add_sub->args[0];
      auto const1 = inner_add_sub->args[1];
      auto const2 = instr->args[1];
      if (instr->subtype == (u32)BinaryInstrSubType::IntMul) {
        auto a1 = b.build_binary_op(const1, const2, BinaryInstrSubType::IntMul);
        auto res = b.build_binary_op(x, a1, BinaryInstrSubType::IntMul);
        worklist.emplace_back(a1.as_instr(), bb);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
      } else if (instr->subtype == (u32)BinaryInstrSubType::IntSDiv) {
        auto a1 = b.build_binary_op(const1, const2, BinaryInstrSubType::IntMul);
        auto res = b.build_binary_op(x, a1, BinaryInstrSubType::IntSDiv);
        worklist.emplace_back(a1.as_instr(), bb);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
      } else if (instr->subtype == (u32)BinaryInstrSubType::IntUDiv) {
        auto a1 = b.build_binary_op(const1, const2, BinaryInstrSubType::IntMul);
        auto res = b.build_binary_op(x, a1, BinaryInstrSubType::IntUDiv);
        worklist.emplace_back(a1.as_instr(), bb);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
      } else if (instr->subtype == (u32)BinaryInstrSubType::And) {
        auto a1 = b.build_binary_op(const1, const2, BinaryInstrSubType::And);
        auto res = b.build_binary_op(x, a1, BinaryInstrSubType::And);
        worklist.emplace_back(a1.as_instr(), bb);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
      } else if (instr->subtype == (u32)BinaryInstrSubType::Or) {
        auto a1 = b.build_binary_op(const1, const2, BinaryInstrSubType::Or);
        auto res = b.build_binary_op(x, a1, BinaryInstrSubType::Or);
        worklist.emplace_back(a1.as_instr(), bb);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
      } else if (instr->subtype == (u32)BinaryInstrSubType::Xor) {
        auto a1 = b.build_binary_op(const1, const2, BinaryInstrSubType::Xor);
        auto res = b.build_binary_op(x, a1, BinaryInstrSubType::Xor);
        worklist.emplace_back(a1.as_instr(), bb);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
      } else if (instr->subtype == (u32)BinaryInstrSubType::Shl) {
        auto a1 = b.build_int_add(const1, const2);
        auto res = b.build_binary_op(x, a1, BinaryInstrSubType::Shl);
        worklist.emplace_back(a1.as_instr(), bb);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
      } else if (instr->subtype == (u32)BinaryInstrSubType::Shr) {
        auto a1 = b.build_int_add(const1, const2);
        auto res = b.build_binary_op(x, a1, BinaryInstrSubType::Shr);
        worklist.emplace_back(a1.as_instr(), bb);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
      }
      instr.destroy();
      return;
    }
  }
  // (x +- constant1) +- constant2
  // x +- (constant1 +- constant2)
  // and
  // (x +- constant1) +- y
  // x +- y) +- constant1
  {
    if ((instr->subtype == (u32)BinaryInstrSubType::IntAdd ||
         instr->subtype == (u32)BinaryInstrSubType::IntSub) &&
        instr->args[0].is_instr()) {
      auto f = [&worklist, &bb, &instr](fir::Instr inner_add_sub,
                                        fir::ValueR v2) {
        if (inner_add_sub->is(InstrType::BinaryInstr) &&
            (inner_add_sub->subtype == (u32)BinaryInstrSubType::IntAdd ||
             inner_add_sub->subtype == (u32)BinaryInstrSubType::IntSub) &&
            inner_add_sub->args[1].is_constant()) {
          fir::Builder b(instr);
          auto x = inner_add_sub->args[0];
          auto const1 = inner_add_sub->args[1];
          if (inner_add_sub->subtype == (u32)BinaryInstrSubType::IntAdd &&
              instr->subtype == (u32)BinaryInstrSubType::IntAdd) {
            fir::ValueR res;
            if (v2.is_constant()) {
              auto a1 = b.build_int_add(const1, v2);
              worklist.emplace_back(a1.as_instr(), bb);
              res = b.build_int_add(x, a1);
            } else {
              auto a1 = b.build_int_add(x, v2);
              worklist.emplace_back(a1.as_instr(), bb);
              res = b.build_int_add(a1, const1);
            }
            push_all_uses(worklist, instr);
            instr->replace_all_uses(res);
          } else if ((inner_add_sub->subtype ==
                          (u32)BinaryInstrSubType::IntSub &&
                      instr->subtype == (u32)BinaryInstrSubType::IntSub)) {
            fir::ValueR res;
            auto a1 = b.build_int_sub(const1, v2);
            res = b.build_int_sub(x, a1);
            worklist.emplace_back(a1.as_instr(), bb);
            push_all_uses(worklist, instr);
            instr->replace_all_uses(res);
          } else if ((inner_add_sub->subtype ==
                          (u32)BinaryInstrSubType::IntAdd &&
                      instr->subtype == (u32)BinaryInstrSubType::IntSub)) {
            auto a1 = b.build_int_sub(const1, v2);
            auto res = b.build_int_add(x, a1);
            worklist.emplace_back(a1.as_instr(), bb);
            push_all_uses(worklist, instr);
            instr->replace_all_uses(res);
          } else if ((inner_add_sub->subtype ==
                          (u32)BinaryInstrSubType::IntSub &&
                      instr->subtype == (u32)BinaryInstrSubType::IntAdd)) {
            auto a1 = b.build_int_sub(v2, const1);
            auto res = b.build_int_add(x, a1);
            worklist.emplace_back(a1.as_instr(), bb);
            push_all_uses(worklist, instr);
            instr->replace_all_uses(res);
          } else {
            UNREACH();
          }
          instr.destroy();
          return true;
        }
        return false;
      };
      if (instr->args[0].is_instr()) {
        auto inner_add_sub = instr->args[0].as_instr();
        auto const2 = instr->args[1];
        if (f(inner_add_sub, const2)) {
          return;
        }
      }
      if (instr->args[1].is_instr()) {
        auto inner_add_sub = instr->args[1].as_instr();
        auto const2 = instr->args[0];
        if (f(inner_add_sub, const2)) {
          return;
        }
      }
    }
  }
  // (x +- constant1) * constant2
  // (x*constant2 +- constant1*constant2)
  {
    if (instr->subtype == (u32)BinaryInstrSubType::IntMul &&
        instr->args[1].is_constant() && instr->args[0].is_instr()) {
      auto inner_add_sub = instr->args[0].as_instr();
      if (inner_add_sub->is(InstrType::BinaryInstr) &&
          (inner_add_sub->subtype == (u32)BinaryInstrSubType::IntAdd ||
           inner_add_sub->subtype == (u32)BinaryInstrSubType::IntSub ||
           inner_add_sub->subtype == (u32)BinaryInstrSubType::IntMul) &&
          inner_add_sub->args[1].is_constant()) {
        fir::Builder b(instr);
        auto const2 = instr->args[1];
        auto x = inner_add_sub->args[0];
        auto const1 = inner_add_sub->args[1];
        if (inner_add_sub->subtype == (u32)BinaryInstrSubType::IntMul) {
          auto a1 = b.build_int_mul(const1, const2);
          auto a2 = b.build_int_mul(x, a1);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(a2);
        } else {
          auto a1 = b.build_int_mul(x, const2);
          auto a2 = b.build_int_mul(const1, const2);
          if (inner_add_sub->subtype == (u32)BinaryInstrSubType::IntAdd) {
            auto res = b.build_int_add(a1, a2);
            push_all_uses(worklist, instr);
            instr->replace_all_uses(res);
          } else {
            auto res = b.build_int_sub(a1, a2);
            push_all_uses(worklist, instr);
            instr->replace_all_uses(res);
          }
        }
        instr.destroy();
        return;
      }
    }
  }

  const ConstantValue *c0_val = nullptr;
  const ConstantValue *c1_val = nullptr;
  if (instr->args[0].is_constant()) {
    c0_val = instr->args[0].as_constant().get_raw_ptr();
  }
  if (instr->args[1].is_constant()) {
    c1_val = instr->args[1].as_constant().get_raw_ptr();
  }
  const bool has_const = (c0_val != nullptr) || (c1_val != nullptr);
  // const bool both_const = has_const && instr->args[0].is_constant();
  if (has_const) {
    if (((c0_val != nullptr) && c0_val->is_poison()) ||
        ((c1_val != nullptr) && c1_val->is_poison())) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(ValueR{ctx->get_poisson_value(instr.get_type())});
      instr.destroy();
      return;
    }
    if (((c0_val != nullptr) && c0_val->is_null()) ||
        ((c1_val != nullptr) && c1_val->is_null())) {
      auto index = ((c0_val != nullptr) && c0_val->is_null()) ? 1 : 0;
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[index]);
      instr.destroy();
      return;
    }

    if ((c0_val != nullptr) && (c1_val != nullptr)) {
      if ((c1_val->type->is_int() && c0_val->type->is_int()) ||
          (c1_val->type->is_ptr() && c0_val->type->is_ptr())) {
        // TODO: this is annoying but idk how to handle it better
        push_all_uses(worklist, instr);
        if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                     c0_val->as_int(), c1_val->as_int(),
                                     c1_val->type, ctx)) {
          instr.destroy();
          return;
        }
      } else if (c1_val->type->is_float() && c1_val->type->as_float() == 32 &&
                 c0_val->type->is_float() && c0_val->type->as_float() == 32) {
        // TODO: this is annoying but idk how to handle it better
        push_all_uses(worklist, instr);
        if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                     c0_val->as_f32(), c1_val->as_f32(),
                                     c1_val->type, ctx)) {
          instr.destroy();
          return;
        }
      } else if (c1_val->type->is_float() && c1_val->type->as_float() == 64 &&
                 c0_val->type->is_float() && c0_val->type->as_float() == 64) {
        // TODO: this is annoying but idk how to handle it better
        push_all_uses(worklist, instr);
        if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                     c0_val->as_f64(), c1_val->as_f64(),
                                     c1_val->type, ctx)) {
          instr.destroy();
          return;
        }
      }
    }

    const auto *c_val = (c1_val != nullptr) ? c1_val : c0_val;
    const u32 c_idx = (c0_val != nullptr) ? 0 : 1;
    const u32 v_idx = (c1_val != nullptr) ? 0 : 1;

    // (x+c1) +y => (x+y)+c1
    if (instr->subtype == (u32)BinaryInstrSubType::IntAdd &&
        instr->args[v_idx].is_instr()) {
      auto sub = instr->args[v_idx].as_instr();
      if (sub->subtype == (u32)BinaryInstrSubType::IntAdd &&
          (sub->args[1].is_constant() &&
           sub->args[1].as_constant()->is_int())) {
        auto x = instr->args[1 - v_idx];
        auto y = sub->args[0];
        auto consti = sub->args[1];
        Builder bb{instr};

        auto inter = bb.build_binary_op(x, y, BinaryInstrSubType::IntAdd);
        auto res =
            bb.build_binary_op(inter, consti, BinaryInstrSubType::IntAdd);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
        instr.destroy();
        return;
      }
    }
    if (instr->subtype == (u32)BinaryInstrSubType::Xor && c_val->is_int() &&
        c_val->as_int() == 1 && instr->args[v_idx].is_instr()) {
      auto arg0 = instr->args[v_idx].as_instr();
      if (arg0->is(InstrType::ZExt) && arg0->args[0].get_type()->is_int() &&
          arg0->args[0].get_type()->as_int() == 1) {
        fir::Builder bu{instr};
        auto r = bu.build_unary_op(arg0->args[0], UnaryInstrSubType::Not);
        auto res = bu.build_zext(r, arg0->get_type());
        worklist.push_back({r.as_instr(), r.as_instr()->get_parent()});
        worklist.push_back({res.as_instr(), res.as_instr()->get_parent()});
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
        instr.destroy();
        return;
      }
    }

    if ((instr->is(BinaryInstrSubType::Shl) ||
         instr->is(BinaryInstrSubType::Shr)) &&
        instr->args[0].is_instr()) {
      auto argi = instr->args[0].as_instr();
      if (instr->is(BinaryInstrSubType::Shr) &&
          argi->is(fir::BinaryInstrSubType::Shl)) {
        // TODO: could also do it without being exactly equal
        //  idk if worht then tho
        // since for x >> n << c
        // it would need still a (x & c1) << c2
        // which would be (x & ((1 << n) - 1)) << (n-c)
        // c2/(n-c) only cancels out if the width is the same
        // or if you propagate it through it would be
        // ((x << (n-c)) & (((1 << n) - 1) << (n-c)))
        if (instr->args[1].is_constant() && instr->args[1] == argi->args[1]) {
          fir::Builder buh{argi};
          // (x & ((1 << n) - 1))
          auto shift_amount = instr->args[1].as_constant()->as_int();
          auto res = buh.build_binary_op(
              argi->args[0],
              fir::ValueR{ctx->get_constant_value(((i128)1 << shift_amount) - 1,
                                                  instr->get_type())},
              BinaryInstrSubType::And);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
          instr.destroy();
          return;
        }
      }
      if (argi->is(fir::BinaryInstrSubType::And) ||
          argi->is(fir::BinaryInstrSubType::Or) ||
          argi->is(fir::BinaryInstrSubType::Xor)) {
        fir::Builder buh{argi};
        fir::ValueR arg1Shift;
        fir::ValueR arg2Shift;
        arg1Shift =
            buh.build_binary_op(argi->args[0], instr->args[1],
                                (BinaryInstrSubType)instr->get_instr_subtype());
        arg2Shift =
            buh.build_binary_op(argi->args[1], instr->args[1],
                                (BinaryInstrSubType)instr->get_instr_subtype());
        auto res = buh.build_binary_op(
            arg1Shift, arg2Shift,
            (fir::BinaryInstrSubType)argi->get_instr_subtype());
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
        instr.destroy();
        return;
      }
    }

    if (c_val->is_float() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::FloatMul) {
      Builder bb{instr};
      if (c_val->as_f64() == -1) {
        auto new_neg =
            bb.build_unary_op(instr->args[v_idx], UnaryInstrSubType::FloatNeg);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_neg);
        instr.destroy();
        return;
      }
      if (c_val->as_f64() == 1) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntURem) {
      auto val = c_val->as_int();
      if (utils::is_pow2(val)) {
        auto mask = ((i128)1 << utils::npow2(val)) - 1;
        fir::Builder bb{instr};
        auto new_res = bb.build_binary_op(
            instr->args[1 - c_idx],
            fir::ValueR{ctx->get_constant_value(mask, instr->get_type())},
            BinaryInstrSubType::And);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_res);
        instr.destroy();
        return;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::Xor) {
      auto val = c_val->as_int();
      if (val == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return;
      }
      i128 all_one_mask = ~(i128)0;
      auto shift_width = (128 - instr->get_type()->as_int());
      auto sign_extended_val = (val << shift_width) >> shift_width;
      if (sign_extended_val == all_one_mask) {
        fir::Builder bb{instr};
        auto new_res =
            bb.build_unary_op(instr->args[1 - c_idx], UnaryInstrSubType::Not);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_res);
        instr.destroy();
        return;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::Or) {
      if (c_val->as_int() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[1 - c_idx]);
        instr.destroy();
        return;
      }
      auto max_unsigend_val =
          (((u128)1 << c_val->get_type()->get_bitwidth()) - 1);
      if (std::bit_cast<u128>(c_val->as_int()) == max_unsigend_val) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(
            std::bit_cast<i128>(max_unsigend_val), instr->get_type())});
        instr.destroy();
        return;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::And) {
      if (std::bit_cast<u128>(c_val->as_int()) ==
          (((u128)1 << c_val->get_type()->get_bitwidth()) - 1)) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[1 - c_idx]);
        instr.destroy();
        return;
      }
      if (c_val->as_int() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value(0, instr.get_type())});
        instr.destroy();
        return;
      }
    }
    if (c_idx == 1 && c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntSub) {
      if (c_val->as_int() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntMul) {
      auto c_vali = c_val->as_int();
      if (c_vali == 1) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return;
      }
      if (c_vali == 0) {
        auto zero_const = ctx.data->get_constant_value(0, c_val->get_type());
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR{zero_const});
        instr.destroy();
        return;
      }
      if (instr->args[v_idx].is_instr()) {
        auto v_i = instr->args[v_idx].as_instr();
        auto uval = std::bit_cast<u128>(c_vali);
        if (utils::is_pow2(uval) && v_i->is(InstrType::BinaryInstr) &&
            (v_i->subtype == (u32)BinaryInstrSubType::IntSDiv ||
             v_i->subtype == (u32)BinaryInstrSubType::IntUDiv) &&
            v_i->args[1].is_constant() &&
            v_i->args[1].as_constant()->eql(*c_val)) {
          fir::Builder b{instr};

          auto magic_constant =
              ctx->get_constant_int(~((i128)1 << (utils::npow2(uval) - 1)),
                                    instr->get_type()->get_bitwidth());
          auto res =
              b.build_binary_op(v_i->args[0], fir::ValueR{magic_constant},
                                BinaryInstrSubType::And);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
          instr.destroy();
          return;
        }
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntAdd) {
      if (c_val->as_int() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return;
      }
      if (instr->args[0].is_instr() && c1_val->get_type()->is_int()) {
        ASSERT(c0_val == nullptr);
        auto a0 = instr->args[0].as_instr();
        if (a0->is(InstrType::BinaryInstr) && a0->args[1].is_constant() &&
            a0->args[1].as_constant()->is_int()) {
          auto sec_constant = a0->args[1].as_constant()->as_int();
          // fmt::println("{}", instr);
          // FIXME: fix potential issue with overflow
          auto biggest_bitwidth = std::max(a0->args[1].get_type()->as_int(),
                                           c1_val->get_type()->as_int());

          switch ((BinaryInstrSubType)a0->subtype) {
            case fir::BinaryInstrSubType::INVALID:
            case fir::BinaryInstrSubType::FloatAdd:
            case fir::BinaryInstrSubType::FloatSub:
            case fir::BinaryInstrSubType::FloatMul:
            case fir::BinaryInstrSubType::FloatDiv:
              UNREACH();
            case fir::BinaryInstrSubType::IntSDiv:
            case fir::BinaryInstrSubType::IntUDiv:
            case fir::BinaryInstrSubType::IntMul:
              break;
            case fir::BinaryInstrSubType::IntAdd: {
              auto new_val = ctx->get_constant_int(
                  c1_val->as_int() + sec_constant, biggest_bitwidth);
              instr.replace_arg(0, a0->args[0]);
              instr.replace_arg(1, ValueR(new_val));
              push_all_uses(worklist, instr);
              break;
            }
            case fir::BinaryInstrSubType::IntSub: {
              auto new_val = ctx->get_constant_int(
                  c1_val->as_int() - sec_constant, biggest_bitwidth);
              instr.replace_arg(0, a0->args[0]);
              instr.replace_arg(1, ValueR(new_val));
              push_all_uses(worklist, instr);
              break;
            }
            default:
              break;
              // fmt::println("Previous op {}", a0);
              // UNREACH();
          }
        }
      }
    }
  }

  // bit patterns
  // if (instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntAdd &&
  //     instr->args[1].is_constant() &&
  //     instr->args[1].as_constant()->is_int())
  //     {
  // const auto *arg0_known =
  //     man.get_or_create_analysis<KnownBits>(instr->args[0]);
  // man.run();
  // TODO: IDK if this is worth it if we do it to early
  //  auto c = instr->args[1].as_constant()->as_int();
  //  if ((arg0_known->known_zero & c) == c) {
  //    fir::Builder bb(instr);
  //    auto new_val = bb.build_binary_op(instr->args[0], instr->args[1],
  //                                      BinaryInstrSubType::Or);
  //    push_all_uses(worklist, instr);
  //    instr->replace_all_uses(ValueR{new_val});
  //    instr.destroy();
  //    return;
  //  }
  // }

  if (instr->get_instr_subtype() == (u32)BinaryInstrSubType::And ||
      instr->get_instr_subtype() == (u32)BinaryInstrSubType::Or) {
    const auto *arg0_known =
        man.get_or_create_analysis<KnownBits>(instr->args[0]);
    const auto *arg1_known =
        man.get_or_create_analysis<KnownBits>(instr->args[1]);
    man.run(ctx);

    auto is_redundant0 = false;
    auto is_redundant1 = false;
    switch ((BinaryInstrSubType)instr->get_instr_subtype()) {
      case fir::BinaryInstrSubType::And:
        is_redundant0 = (~arg0_known->known_zero & ~arg1_known->known_one) == 0;
        is_redundant1 = (~arg1_known->known_zero & ~arg0_known->known_one) == 0;
        break;
      case fir::BinaryInstrSubType::Or:
        is_redundant0 = (~arg0_known->known_one & ~arg1_known->known_zero) == 0;
        is_redundant1 = (~arg1_known->known_one & ~arg0_known->known_zero) == 0;
        break;
      case fir::BinaryInstrSubType::Xor:
        is_redundant0 = arg1_known->known_zero == ~0ULL;
        is_redundant1 = arg0_known->known_zero == ~0ULL;
        break;
      case fir::BinaryInstrSubType::INVALID:
      case fir::BinaryInstrSubType::IntAdd:
      case fir::BinaryInstrSubType::IntSub:
      case fir::BinaryInstrSubType::IntMul:
      case fir::BinaryInstrSubType::IntSRem:
      case fir::BinaryInstrSubType::IntURem:
      case fir::BinaryInstrSubType::IntSDiv:
      case fir::BinaryInstrSubType::IntUDiv:
      case fir::BinaryInstrSubType::Shl:
      case fir::BinaryInstrSubType::Shr:
      case fir::BinaryInstrSubType::AShr:
      case fir::BinaryInstrSubType::FloatAdd:
      case fir::BinaryInstrSubType::FloatSub:
      case fir::BinaryInstrSubType::FloatMul:
      case fir::BinaryInstrSubType::FloatDiv:
        break;
    }
    if (is_redundant0) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[0]);
      instr.destroy();
      return;
    }
    if (is_redundant1) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[1]);
      instr.destroy();
      return;
    }
  }

  if (simplify_reduction(instr, bb, ctx, worklist, man)) {
    return;
  }
}

void simplify_icmp(fir::Instr instr, fir::BasicBlock /*bb*/, fir::Context &ctx,
                   WorkList &worklist, AttributerManager &man) {
  using namespace foptim::fir;
  {
    if ((instr->args[0].is_constant() &&
         (!instr->args[0].as_constant()->is_global() &&
          !instr->args[0].as_constant()->is_func())) &&
        (!instr->args[1].is_constant() ||
         (instr->args[1].as_constant()->is_global() ||
          instr->args[1].as_constant()->is_func()))) {
      swap_args_icmp(instr);
    }
  }

  bool first_constant = instr->args[0].is_constant();
  bool second_constant = instr->args[1].is_constant();

  if (first_constant && second_constant) {
    const auto c1 = instr->args[0].as_constant();
    const auto c2 = instr->args[1].as_constant();
    // TODO: poisson should just return a poisson value
    if (c1->is_poison() || c2->is_poison()) {
      auto new_const_value = ctx->get_poisson_value(ctx->get_int_type(8));
      push_all_uses(worklist, instr);
      instr->replace_all_uses(ValueR(new_const_value));
      ASSERT(instr->bbs.size() == 0);
      instr.destroy();
      return;
    }

    {
      bool is_eq =
          (ICmpInstrSubType)instr->get_instr_subtype() == ICmpInstrSubType::EQ;
      bool is_neq =
          (ICmpInstrSubType)instr->get_instr_subtype() == ICmpInstrSubType::NE;
      if ((is_eq || is_neq)) {
        bool first_known_non_null =
            (c1->is_global() || (c1->is_func() && !c1->as_func()->is_decl()));
        bool second_known_non_null =
            (c2->is_global() || (c2->is_func() && !c1->as_func()->is_decl()));
        bool first_known_null = c1->is_null() || c1->is_poison();
        bool second_known_null = c2->is_null() || c2->is_poison();
        bool is_true = false;
        bool is_known = false;
        if (first_known_non_null && second_known_null) {
          is_true = is_neq;
          is_known = true;
        }
        if (first_known_null && second_known_non_null) {
          is_true = is_neq;
          is_known = true;
        }
        if (first_known_non_null && second_known_non_null) {
          is_true = is_eq;
          is_known = true;
        }
        if (first_known_null && second_known_null) {
          is_true = is_eq;
          is_known = true;
        }

        if (is_known) {
          auto new_const_value = ctx->get_constant_int((u32)is_true, 8);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(ValueR(new_const_value));
          instr.destroy();
          return;
        }
      }
    }
    if (c1->is_global() || c1->is_func() || c2->is_global() || c2->is_func()) {
      return;
    }
    i128 v1 = 0;
    i128 v2 = 0;
    if (c1->is_int()) {
      v1 = c1->as_int();
    } else if (c1->is_null() || c1->is_poison()) {
    } else {
      fmt::println("{}", instr);
      fmt::println("{}", c1);
      TODO("IMPL");
    }
    if (c2->is_int()) {
      v2 = c2->as_int();
    } else if (c2->is_null() || c2->is_poison()) {
    } else {
      fmt::println("{}", instr);
      fmt::println("{}", c2);
      TODO("IMPL");
    }

    auto bitwidth =
        std::max(c1->type->get_bitwidth(), c2->type->get_bitwidth());
    auto mask = ((i128)1 << bitwidth) - 1;
    auto rest_width = (128 - bitwidth);
    v1 = ((v1 & mask) << rest_width) >> rest_width;
    v2 = ((v2 & mask) << rest_width) >> rest_width;
    bool is_true = false;
    switch ((ICmpInstrSubType)instr->get_instr_subtype()) {
      case fir::ICmpInstrSubType::INVALID:
        UNREACH();
      case fir::ICmpInstrSubType::SLT:
        is_true = (i64)v1 < (i64)v2;
        break;
      case fir::ICmpInstrSubType::ULT:
        is_true = (u64)v1 < (u64)v2;
        break;
      case fir::ICmpInstrSubType::NE:
        is_true = v1 != v2;
        break;
      case fir::ICmpInstrSubType::EQ:
        is_true = v1 == v2;
        break;
      case fir::ICmpInstrSubType::SGT:
        is_true = (i64)v1 > (i64)v2;
        break;
      case fir::ICmpInstrSubType::UGT:
        is_true = (u64)v1 > (u64)v2;
        break;
      case fir::ICmpInstrSubType::UGE:
        is_true = (u64)v1 >= (u64)v2;
        break;
      case fir::ICmpInstrSubType::ULE:
        is_true = (u64)v1 <= (u64)v2;
        break;
      case fir::ICmpInstrSubType::SGE:
        is_true = (i64)v1 >= (i64)v2;
        break;
      case fir::ICmpInstrSubType::SLE:
        is_true = (i64)v1 <= (i64)v2;
        break;
      case fir::ICmpInstrSubType::MulOverflow: {
        i128 output = v1 * v2;
        i128 mask = ~(((i128)1 << bitwidth) - 1);
        is_true = (output & mask) != 0;
      } break;

      case fir::ICmpInstrSubType::AddOverflow: {
        i128 output = v1 + v2;
        i128 mask = ~(((i128)1 << bitwidth) - 1);
        is_true = (output & mask) != 0;
      } break;
    }
    auto new_const_value = ctx->get_constant_int((u64)is_true, 1);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(ValueR(new_const_value));
    instr.destroy();
    return;
  }

  if (second_constant && instr->args[1].as_constant()->is_int()) {
    auto sub_type = (ICmpInstrSubType)instr->get_instr_subtype();
    i128 c_val = instr->args[1].as_constant()->as_int();

    if (instr->args[0].is_instr()) {
      auto arg0 = instr->args[0].as_instr();
      // NOTE: a / 2^x > 0 -> a > x (if x is between 0-bitwidth)
      if (c_val == 0 && (sub_type == ICmpInstrSubType::SGT) &&
          arg0->is(InstrType::BinaryInstr) &&
          arg0->subtype == (u32)BinaryInstrSubType::IntSDiv &&
          arg0->args[1].is_constant() &&
          // TODO: could also be poision but that gets propagated anyway
          arg0->args[1].as_constant()->is_int()) {
        // auto a = instr->args[1].as_constant()->as_int();
        auto x = arg0->args[1].as_constant()->as_int();
        if (x > 0 && (x & (x - 1)) == 0 && x < arg0->get_type()->as_int()) {
          Builder bb{instr};
          auto new_val = bb.build_int_cmp(arg0->args[0], arg0->args[1],
                                          ICmpInstrSubType::SGE);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(ValueR(new_val));
          instr.destroy();
          return;
        }
      }

      if ((sub_type == ICmpInstrSubType::SLE ||
           sub_type == ICmpInstrSubType::SLT) &&
          arg0->is(InstrType::BinaryInstr) &&
          arg0->subtype == (u32)BinaryInstrSubType::IntSDiv &&
          arg0->args[1].is_constant()) {
        bool is_or_eq = sub_type == ICmpInstrSubType::SLE;
        auto x = arg0->args[0];
        auto c_div = arg0->args[1].as_constant()->as_int();
        Builder bb{instr};
        auto new_val = bb.build_int_cmp(
            x,
            fir::ValueR{ctx->get_constant_value(
                (c_val + (is_or_eq ? 1 : 0)) * c_div, x.get_type())},
            ICmpInstrSubType::SLE);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR(new_val));
        instr.destroy();
        return;
      }
      if ((sub_type == ICmpInstrSubType::SGE ||
           sub_type == ICmpInstrSubType::SGT) &&
          arg0->is(InstrType::BinaryInstr) &&
          arg0->subtype == (u32)BinaryInstrSubType::IntSDiv &&
          arg0->args[1].is_constant()) {
        bool is_or_eq = sub_type == ICmpInstrSubType::SGE;
        auto x = arg0->args[0];
        auto c_div = arg0->args[1].as_constant()->as_int();
        Builder bb{instr};
        auto new_val = bb.build_int_cmp(
            x,
            fir::ValueR{ctx->get_constant_value(
                (c_val + (is_or_eq ? 0 : 1)) * c_div, x.get_type())},
            ICmpInstrSubType::SGE);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR(new_val));
        instr.destroy();
        return;
      }
    }

    if ((sub_type == ICmpInstrSubType::SLT) && instr->args[0].is_instr()) {
      auto arg0 = instr->args[0].as_instr();
      if (arg0->is(InstrType::Intrinsic) &&
          arg0->subtype == (u32)IntrinsicSubType::Abs && c_val == 1) {
        // abs(x) < 1 => x == 0
        auto x = arg0->args[0];
        Builder bb{instr};
        auto new_val = bb.build_int_cmp(
            x, fir::ValueR{ctx->get_constant_value(0, x.get_type())},
            ICmpInstrSubType::EQ);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR(new_val));
        instr.destroy();
        return;
      }
    }

    if ((sub_type == ICmpInstrSubType::MulOverflow) &&
        instr->args[0].is_instr() &&
        instr->args[0].as_instr()->is(fir::InstrType::SExt)) {
      // if we multiply 2 32bit values with 1 being sign extended it can
      // only be the case if
      //  the sign extnded one was negative
      if ((u128)c_val <= std::numeric_limits<u32>::max()) {
        auto x = instr->args[0].as_instr()->args[0];
        Builder bb{instr};
        auto new_val = bb.build_int_cmp(
            x, fir::ValueR{ctx->get_constant_value(0, x.get_type())},
            ICmpInstrSubType::SLT);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR(new_val));
        instr.destroy();
        return;
      }
    }

    if ((sub_type == ICmpInstrSubType::EQ ||
         sub_type == ICmpInstrSubType::NE) &&
        instr->args[0].is_instr()) {
      auto arg0 = instr->args[0].as_instr();
      if (arg0->is(InstrType::BinaryInstr) &&
          arg0->subtype == (u32)BinaryInstrSubType::IntSub && c_val == 0) {
        fir::Builder b{instr};
        auto new_cmp = b.build_int_cmp(arg0->args[0], arg0->args[1], sub_type);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_cmp);
        instr.destroy();
        return;
      }
      if (arg0->get_type()->as_int() == 1 && c_val == 0) {
        bool needs_negation = sub_type == ICmpInstrSubType::EQ;
        if (needs_negation) {
          fir::Builder b{instr};
          auto negated =
              b.build_unary_op(instr->args[0], UnaryInstrSubType::Not);
          push_all_uses(worklist, instr);
          worklist.push_back(
              {negated.as_instr(), negated.as_instr()->get_parent()});
          instr->replace_all_uses(negated);
          instr.destroy();
          return;
        }
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return;
      }
      if (arg0->is(InstrType::ZExt) ||
          (arg0->is(InstrType::SExt) && c_val == 0)) {
        // zext(x) == 0 => x == C
        // zext(x) != 0 => x != C
        // sext(x) == 0 => x == 0
        // sext(x) != 0 => x != 0
        auto x = arg0->args[0];
        Builder bb{instr};
        instr.replace_arg(0, x);
        instr.replace_arg(
            1, fir::ValueR{ctx->get_constant_value(c_val, x.get_type())});
        worklist.emplace_back(instr, instr->get_parent());
        return;
      }
    }

    bool check_negative = false;
    bool check_positive = false;

    // TODO : theres more cases like eq with a positive constant
    check_negative = (sub_type == fir::ICmpInstrSubType::SLT && c_val == 0) ||
                     (sub_type == fir::ICmpInstrSubType::SLE && c_val == -1);
    check_positive = (sub_type == fir::ICmpInstrSubType::SGT && c_val == 0) ||
                     (sub_type == fir::ICmpInstrSubType::SGE && c_val == 1);
    if (check_positive || check_negative) {
      const auto *bits = man.get_or_create_analysis<KnownBits>(instr->args[0]);
      man.run(ctx);
      auto msb_res = bits->msb_info();
      bool value_known_negative = msb_res == KnownBits::KnownOne;
      bool value_known_positive = msb_res == KnownBits::KnownZero;
      bool evals_to_true = (value_known_negative && check_negative) ||
                           (value_known_positive && check_positive);
      bool evals_to_false = (value_known_negative && check_positive) ||
                            (value_known_positive && check_negative);
      if (evals_to_false || evals_to_true) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            ValueR(ctx->get_constant_int(evals_to_true ? 1 : 0, 1)));
        instr.destroy();
        return;
      }
    }
  }

  if (instr->args[0].is_instr()) {
    auto a1i = instr->args[0].as_instr();
    if (a1i->is(InstrType::BinaryInstr) &&
        a1i->subtype == (u32)BinaryInstrSubType::And &&
        a1i->args[1].is_constant()) {
      auto const_and = a1i->args[1].as_constant()->as_int();
      // could also check <= and insert a itrunc previously
      for (auto amount : {8, 16, 32}) {
        if (const_and == (1UL << amount) - 1) {
          fir::Builder b{instr};
          auto truncated_val =
              b.build_itrunc(a1i->args[0], ctx->get_int_type(amount));
          worklist.push_back({truncated_val.as_instr(),
                              truncated_val.as_instr()->get_parent()});
          instr.replace_arg(0, truncated_val);
          auto truncated_o =
              b.build_itrunc(instr->args[1], ctx->get_int_type(amount));
          worklist.push_back({truncated_val.as_instr(),
                              truncated_val.as_instr()->get_parent()});
          worklist.push_back({truncated_val.as_instr(),
                              truncated_val.as_instr()->get_parent()});
          worklist.push_back(
              {truncated_o.as_instr(), truncated_o.as_instr()->get_parent()});
          instr.replace_arg(1, truncated_o);
          return;
        }
      }
    }
  }

  auto sub_type = (ICmpInstrSubType)instr->get_instr_subtype();
  {
    const auto *bits1 = man.get_or_create_analysis<KnownBits>(instr->args[0]);
    const auto *bits2 = man.get_or_create_analysis<KnownBits>(instr->args[1]);
    man.run(ctx);
    if (sub_type == ICmpInstrSubType::UGT ||
        sub_type == ICmpInstrSubType::UGE ||
        sub_type == ICmpInstrSubType::ULT ||
        sub_type == ICmpInstrSubType::ULE) {
      bool evals_to_true = false;
      bool evals_to_false = false;
      if (sub_type == ICmpInstrSubType::UGT) {
        evals_to_true =
            bits1->get_unsigned_min_value() > bits2->get_unsigned_max_value();
        evals_to_false =
            bits1->get_unsigned_max_value() <= bits2->get_unsigned_min_value();
      } else if (sub_type == ICmpInstrSubType::UGE) {
        evals_to_true =
            bits1->get_unsigned_min_value() >= bits2->get_unsigned_max_value();
        evals_to_false =
            bits1->get_unsigned_max_value() < bits2->get_unsigned_min_value();
      } else if (sub_type == ICmpInstrSubType::ULT) {
        evals_to_true =
            bits1->get_unsigned_max_value() < bits2->get_unsigned_min_value();
        evals_to_false =
            bits1->get_unsigned_min_value() >= bits2->get_unsigned_max_value();
      } else if (sub_type == ICmpInstrSubType::ULE) {
        evals_to_true =
            bits1->get_unsigned_max_value() <= bits2->get_unsigned_min_value();
        evals_to_false =
            bits1->get_unsigned_min_value() > bits2->get_unsigned_max_value();
      }

      ASSERT(!evals_to_false || !evals_to_true);
      if (evals_to_true || evals_to_false) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            ValueR(ctx->get_constant_int(evals_to_true ? 1 : 0, 1)));
        instr.destroy();
        return;
      }
    }

    if (sub_type == ICmpInstrSubType::SGT ||
        sub_type == ICmpInstrSubType::SGE ||
        sub_type == ICmpInstrSubType::SLT ||
        sub_type == ICmpInstrSubType::SLE) {
      bool evals_to_true = false;
      bool evals_to_false = false;
      if (sub_type == ICmpInstrSubType::SGT) {
        evals_to_true =
            bits1->get_signed_min_value() > bits2->get_signed_max_value();
        evals_to_false =
            bits1->get_signed_max_value() <= bits2->get_signed_min_value();
      } else if (sub_type == ICmpInstrSubType::SGE) {
        evals_to_true =
            bits1->get_signed_min_value() >= bits2->get_signed_max_value();
        evals_to_false =
            bits1->get_signed_max_value() < bits2->get_signed_min_value();
      } else if (sub_type == ICmpInstrSubType::SLT) {
        evals_to_true =
            bits1->get_signed_max_value() < bits2->get_signed_min_value();
        evals_to_false =
            bits1->get_signed_min_value() >= bits2->get_signed_max_value();
      } else if (sub_type == ICmpInstrSubType::SLE) {
        evals_to_true =
            bits1->get_signed_max_value() <= bits2->get_signed_min_value();
        evals_to_false =
            bits1->get_signed_min_value() > bits2->get_signed_max_value();
      }

      ASSERT(!evals_to_false || !evals_to_true);
      if (evals_to_true || evals_to_false) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            ValueR(ctx->get_constant_int(evals_to_true ? 1 : 0, 1)));
        instr.destroy();
        return;
      }
    }

    if (sub_type == ICmpInstrSubType::EQ || sub_type == ICmpInstrSubType::NE) {
      bool evals_to_true = false;
      bool evals_to_false = false;
      if (sub_type == ICmpInstrSubType::EQ) {
        evals_to_false = (bits1->known_one & bits2->known_zero) != 0 ||
                         (bits1->known_zero & bits2->known_one) != 0;
      } else if (sub_type == ICmpInstrSubType::NE) {
        evals_to_true = (bits1->known_one & bits2->known_zero) != 0 ||
                        (bits1->known_zero & bits2->known_one) != 0;
      }

      if (evals_to_true || evals_to_false) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            ValueR(ctx->get_constant_int(evals_to_true ? 1 : 0, 1)));
        instr.destroy();
        return;
      }
    }
  }

  // if first is not a constant we could get still a
  //  y = add x 5
  //  icmp y 100 -> which then can be simplified
  // if (!first_constant && second_constant && instr->args[0].is_instr()) {
  //   auto arg_instr = instr->args[0].as_instr();
  //   auto constant = instr->args[1].as_constant()->as_int();

  //   if (arg_instr->is(InstrType::BinaryInstr) &&
  //       arg_instr->args[1].is_constant() &&
  //       arg_instr->args[1].as_constant()->is_int() &&
  //       (BinaryInstrSubType)arg_instr->subtype ==
  //       BinaryInstrSubType::IntAdd)
  //       {
  //     auto constant2 = arg_instr->args[1].as_constant()->as_int();
  //     instr.replace_arg(0, arg_instr->args[0]);
  //     instr.replace_arg(1, ValueR(ctx->get_constant_value(
  //                              constant - constant2,
  //                              arg_instr->get_type())));
  //     return;
  //   }
  // }
}

void simplify_fcmp(fir::Instr instr, fir::BasicBlock /*bb*/, fir::Context &ctx,
                   WorkList &worklist) {
  using namespace foptim::fir;
  {
    if ((instr->args[0].is_constant() &&
         (!instr->args[0].as_constant()->is_global() &&
          !instr->args[0].as_constant()->is_func())) &&
        (!instr->args[1].is_constant() ||
         (instr->args[1].as_constant()->is_global() ||
          instr->args[1].as_constant()->is_func()))) {
      swap_args_fcmp(instr);
    }
  }

  bool first_constant = instr->args[0].is_constant();
  bool second_constant = instr->args[1].is_constant();

  // conert to integer comparison
  if (instr->args[0].is_instr() && second_constant) {
    auto arg0_i = instr->args[0].as_instr();
    auto const1_i = instr->args[1].as_constant();
    if (const1_i->is_float() && arg0_i->is(InstrType::Conversion) &&
        arg0_i->subtype == (u32)ConversionSubType::SITOFP) {
      fir::Builder b{instr};
      // TODO: this is iffy
      auto const_val = const1_i->as_f64();
      switch ((FCmpInstrSubType)instr->get_instr_subtype()) {
        case fir::FCmpInstrSubType::OLT: {
          auto r = b.build_int_cmp(
              arg0_i->args[0],
              fir::ValueR{ctx->get_constant_value((i128)ceil(const_val),
                                                  arg0_i->args[0].get_type())},
              ICmpInstrSubType::SLT);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(r);
          instr.destroy();
          return;
        }
        case fir::FCmpInstrSubType::INVALID:
        case fir::FCmpInstrSubType::AlwFalse:
        case fir::FCmpInstrSubType::OEQ:
        case fir::FCmpInstrSubType::OGT:
        case fir::FCmpInstrSubType::OGE:
        case fir::FCmpInstrSubType::OLE:
        case fir::FCmpInstrSubType::ONE:
        case fir::FCmpInstrSubType::ORD:
        case fir::FCmpInstrSubType::UNO:
        case fir::FCmpInstrSubType::UEQ:
        case fir::FCmpInstrSubType::UGT:
        case fir::FCmpInstrSubType::UGE:
        case fir::FCmpInstrSubType::ULT:
        case fir::FCmpInstrSubType::ULE:
        case fir::FCmpInstrSubType::UNE:
        case fir::FCmpInstrSubType::AlwTrue:
        case fir::FCmpInstrSubType::IsNaN:
          fmt::println("{}", instr);
          TODO("impl");
          break;
      }
    }
  }

  if (first_constant && second_constant) {
    const auto c1 = instr->args[0].as_constant();
    const auto c2 = instr->args[1].as_constant();
    if (c1->is_poison() || c2->is_poison()) {
      auto new_const_value = ctx->get_poisson_value(ctx->get_int_type(8));
      push_all_uses(worklist, instr);
      instr->replace_all_uses(ValueR(new_const_value));
      instr.destroy();
      return;
    }
    ASSERT(c1->is_float());
    const auto v1 = c1->as_float();
    const auto v2 = c2->as_float();

    bool is_true = false;
    // IMPORTANT: !!THIS IS IN OTHER SYNTAX SO FLIPPED ARGUMETNS!!
    // IMPORTANT: !!THIS IS IN OTHER SYNTAX SO FLIPPED ARGUMETNS!!
    switch ((FCmpInstrSubType)instr->get_instr_subtype()) {
      case fir::FCmpInstrSubType::IsNaN:
        __asm__(
            "vcomisd %2, %1\n\t"
            "setp %0"
            : "=r"(is_true)
            : "x"(v1), "x"(v2));
        break;
      case fir::FCmpInstrSubType::OEQ:
        __asm__(
            "vcomisd %2, %1\n\t"
            "sete %0"
            : "=r"(is_true)
            : "x"(v1), "x"(v2));
        break;
      case fir::FCmpInstrSubType::OGT:
        __asm__(
            "vcomisd %2, %1\n\t"
            "seta %0"
            : "=r"(is_true)
            : "x"(v1), "x"(v2));
        break;
      case fir::FCmpInstrSubType::OGE:
        __asm__(
            "vcomisd %2, %1\n\t"
            "setae %0"
            : "=r"(is_true)
            : "x"(v1), "x"(v2));
        break;
      case fir::FCmpInstrSubType::OLT:
        __asm__(
            "vcomisd %2, %1\n\t"
            "setb %0"
            : "=r"(is_true)
            : "x"(v1), "x"(v2));
        break;
      case fir::FCmpInstrSubType::OLE:
        __asm__(
            "vcomisd %2, %1\n\t"
            "setbe %0"
            : "=r"(is_true)
            : "x"(v1), "x"(v2));
        break;
      case fir::FCmpInstrSubType::ONE:
        __asm__(
            "vcomisd %2, %1\n\t"
            "setne %0"
            : "=r"(is_true)
            : "x"(v1), "x"(v2));
        break;
      case fir::FCmpInstrSubType::ORD:
      case fir::FCmpInstrSubType::UNO:
      case fir::FCmpInstrSubType::UEQ:
      case fir::FCmpInstrSubType::UGT:
      case fir::FCmpInstrSubType::UGE:
      case fir::FCmpInstrSubType::ULT:
      case fir::FCmpInstrSubType::ULE:
        IMPL("implement");
      case fir::FCmpInstrSubType::UNE:
        __asm__(
            "vucomisd %2, %1\n\t"
            "setne %0"
            : "=r"(is_true)
            : "x"(v1), "x"(v2));
        break;
      case fir::FCmpInstrSubType::INVALID:
      case fir::FCmpInstrSubType::AlwFalse:
      case fir::FCmpInstrSubType::AlwTrue:
        return;
    }
    auto new_const_value = ctx->get_constant_int((u64)is_true, 8);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(ValueR(new_const_value));
    ASSERT(instr->bbs.size() == 0);
    instr.destroy();
    return;
  }
}

bool is_constant_int_zero(fir::ValueR r) {
  if (!r.is_constant()) {
    return false;
  }
  auto c = r.as_constant();
  if (!c->is_int()) {
    return false;
  }
  return c->as_int() == 0;
}

bool is_constant_float_zero(fir::ValueR r) {
  if (!r.is_constant()) {
    return false;
  }
  auto c = r.as_constant();
  if (!c->is_float()) {
    return false;
  }
  return c->as_float() == 0;
}

bool select_to_abs(fir::Instr instr, WorkList &worklist) {
  auto icmp = instr->args[0].as_instr();
  bool negated = false;
  bool positive = false;
  // fmt::println("{:cd}", icmp);
  // fmt::println("{:cd}", instr);
  if (icmp->args[0] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::UnaryInstrSubType::IntNeg) &&
      instr->args[2].as_instr()->args[0] == icmp->args[0]) {
    positive = true;
  }
  if (icmp->args[0] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::BinaryInstrSubType::IntSub) &&
      is_constant_int_zero(instr->args[2].as_instr()->args[0]) &&
      instr->args[2].as_instr()->args[1] == icmp->args[0]) {
    positive = true;
  }
  if (icmp->args[0] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::UnaryInstrSubType::IntNeg) &&
      instr->args[1].as_instr()->args[0] == icmp->args[0]) {
    negated = true;
  }
  if (icmp->args[0] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::BinaryInstrSubType::IntSub) &&
      is_constant_int_zero(instr->args[1].as_instr()->args[0]) &&
      instr->args[1].as_instr()->args[1] == icmp->args[0]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::UnaryInstrSubType::IntNeg) &&
      instr->args[2].as_instr()->args[0] == icmp->args[1]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::BinaryInstrSubType::IntSub) &&
      is_constant_int_zero(instr->args[2].as_instr()->args[0]) &&
      instr->args[2].as_instr()->args[1] == icmp->args[1]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::UnaryInstrSubType::IntNeg) &&
      instr->args[1].as_instr()->args[0] == icmp->args[1]) {
    positive = true;
  }
  if (icmp->args[1] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::BinaryInstrSubType::IntSub) &&
      is_constant_int_zero(instr->args[1].as_instr()->args[0]) &&
      instr->args[1].as_instr()->args[1] == icmp->args[1]) {
    positive = true;
  }
  ASSERT(!negated || !positive);
  if (negated || positive) {
    fir::Builder b{instr};
    fir::ValueR new_val;
    switch ((fir::ICmpInstrSubType)icmp->subtype) {
      case fir::ICmpInstrSubType::SGT:
      case fir::ICmpInstrSubType::SGE:
      case fir::ICmpInstrSubType::UGT:
      case fir::ICmpInstrSubType::UGE:
        if (positive) {
          new_val =
              b.build_intrinsic(icmp->args[0], fir::IntrinsicSubType::Abs);
          break;
        }
      case fir::ICmpInstrSubType::SLT:
      case fir::ICmpInstrSubType::SLE:
      case fir::ICmpInstrSubType::ULT:
      case fir::ICmpInstrSubType::ULE:
        if (negated) {
          new_val =
              b.build_intrinsic(icmp->args[0], fir::IntrinsicSubType::Abs);
          break;
        }
      default:
        fmt::println("{:cd}", icmp);
        fmt::println("{:cd}", instr);
        TODO("okak");
        break;
    }
    if (!new_val.is_invalid()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(new_val);
      instr.destroy();
      return true;
    }
  }
  return false;
}

bool select_to_fabs(fir::Instr instr, WorkList &worklist) {
  auto icmp = instr->args[0].as_instr();
  bool negated = false;
  bool positive = false;
  // fmt::println("{:cd}", icmp);
  // fmt::println("{:cd}", instr);
  if (icmp->args[0] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::UnaryInstrSubType::FloatNeg) &&
      instr->args[2].as_instr()->args[0] == icmp->args[0]) {
    positive = true;
  }
  if (icmp->args[0] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::BinaryInstrSubType::FloatSub) &&
      is_constant_float_zero(instr->args[2].as_instr()->args[0]) &&
      instr->args[2].as_instr()->args[1] == icmp->args[0]) {
    positive = true;
  }
  if (icmp->args[0] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::UnaryInstrSubType::FloatNeg) &&
      instr->args[1].as_instr()->args[0] == icmp->args[0]) {
    negated = true;
  }
  if (icmp->args[0] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::BinaryInstrSubType::FloatSub) &&
      is_constant_float_zero(instr->args[1].as_instr()->args[0]) &&
      instr->args[1].as_instr()->args[1] == icmp->args[0]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::UnaryInstrSubType::FloatNeg) &&
      instr->args[2].as_instr()->args[0] == icmp->args[1]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::BinaryInstrSubType::FloatSub) &&
      is_constant_float_zero(instr->args[2].as_instr()->args[0]) &&
      instr->args[2].as_instr()->args[1] == icmp->args[1]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::UnaryInstrSubType::FloatNeg) &&
      instr->args[1].as_instr()->args[0] == icmp->args[1]) {
    positive = true;
  }
  if (icmp->args[1] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::BinaryInstrSubType::FloatSub) &&
      is_constant_float_zero(instr->args[1].as_instr()->args[0]) &&
      instr->args[1].as_instr()->args[1] == icmp->args[1]) {
    positive = true;
  }
  ASSERT(!negated || !positive);
  if (negated || positive) {
    fir::Builder b{instr};
    fir::ValueR new_val;
    switch ((fir::FCmpInstrSubType)icmp->subtype) {
      case fir::FCmpInstrSubType::OGE:
        if (positive) {
          new_val =
              b.build_intrinsic(icmp->args[0], fir::IntrinsicSubType::FAbs);
          break;
        }
      case fir::FCmpInstrSubType::OLT:
        if (negated) {
          new_val =
              b.build_intrinsic(icmp->args[0], fir::IntrinsicSubType::FAbs);
          break;
        }
      case fir::FCmpInstrSubType::OGT:
      case fir::FCmpInstrSubType::AlwFalse:
      case fir::FCmpInstrSubType::OEQ:
      case fir::FCmpInstrSubType::OLE:
      case fir::FCmpInstrSubType::ONE:
      case fir::FCmpInstrSubType::ORD:
      case fir::FCmpInstrSubType::UNO:
      case fir::FCmpInstrSubType::UEQ:
      case fir::FCmpInstrSubType::UGT:
      case fir::FCmpInstrSubType::UGE:
      case fir::FCmpInstrSubType::ULT:
      case fir::FCmpInstrSubType::ULE:
      case fir::FCmpInstrSubType::UNE:
      case fir::FCmpInstrSubType::AlwTrue:
      case fir::FCmpInstrSubType::IsNaN:
      case fir::FCmpInstrSubType::INVALID:
      default:
        fmt::println("{:cd}", icmp);
        fmt::println("{:cd}", instr);
        TODO("okak");
        break;
    }
    if (!new_val.is_invalid()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(new_val);
      instr.destroy();
      return true;
    }
  }
  return false;
}

void simplify_select(fir::Instr instr, fir::BasicBlock /*bb*/,
                     fir::Context & /*ctx*/, WorkList &worklist) {
  if (instr->args[0].is_constant()) {
    auto v1 = instr->args[0].as_constant()->as_int();
    push_all_uses(worklist, instr);
    if (v1 == 0) {
      instr->replace_all_uses(instr->args[2]);
    } else {
      instr->replace_all_uses(instr->args[1]);
    }
    instr.destroy();
    return;
  }
  if (instr->args[1] == instr->args[2]) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[1]);
    instr.destroy();
    return;
  }
  if (instr->args[1].is_constant() && instr->args[2].is_constant() &&
      instr->get_type()->is_int()) {
    auto arg1 = instr->args[1].as_constant()->as_int();
    auto arg2 = instr->args[2].as_constant()->as_int();
    auto output_width = instr->get_type()->as_int();
    if ((arg1 == 1 || (output_width == 1 && arg1 != 0)) && arg2 == 0) {
      auto new_val = instr->args[0];
      if (output_width != 1) {
        fir::Builder b(instr);
        new_val = b.build_zext(new_val, instr->get_type());
      }
      push_all_uses(worklist, instr);
      instr->replace_all_uses(new_val);
      instr.destroy();
      return;
    }
    if (arg1 == 0 && (arg2 == 1 || (output_width == 1 && arg2 != 0))) {
      fir::Builder b(instr);
      auto new_val =
          b.build_unary_op(instr->args[0], fir::UnaryInstrSubType::Not);
      if (output_width != 1) {
        new_val = b.build_zext(new_val, instr->get_type());
      }
      push_all_uses(worklist, instr);
      instr->replace_all_uses(new_val);
      instr.destroy();
      return;
    }
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ICmp) &&
      is_constant_int_zero(instr->args[0].as_instr()->args[1])) {
    if (select_to_abs(instr, worklist)) {
      return;
    }
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::FCmp) &&
      is_constant_float_zero(instr->args[0].as_instr()->args[1])) {
    if (select_to_fabs(instr, worklist)) {
      return;
    }
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ICmp)) {
    auto icmp = instr->args[0].as_instr();
    bool negated = false;
    bool positive = false;
    if (icmp->args[1] == instr->args[1] && icmp->args[0] == instr->args[2]) {
      positive = true;
    } else if (icmp->args[1] == instr->args[2] &&
               icmp->args[0] == instr->args[1]) {
      negated = true;
    }
    if (negated || positive) {
      fir::Builder b{instr};
      fir::ValueR new_val;
      switch ((fir::ICmpInstrSubType)icmp->subtype) {
        case fir::ICmpInstrSubType::UGT:
        case fir::ICmpInstrSubType::UGE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::UMax
                                              : fir::IntrinsicSubType::UMin);
          break;
        case fir::ICmpInstrSubType::ULT:
        case fir::ICmpInstrSubType::ULE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::UMin
                                              : fir::IntrinsicSubType::UMax);
          break;
        case fir::ICmpInstrSubType::SLT:
        case fir::ICmpInstrSubType::SLE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::SMin
                                              : fir::IntrinsicSubType::SMax);
          break;
        case fir::ICmpInstrSubType::SGT:
        case fir::ICmpInstrSubType::SGE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::SMax
                                              : fir::IntrinsicSubType::SMin);
          break;
        case fir::ICmpInstrSubType::EQ:
          if (positive && !icmp->args[0].is_constant()) {
            new_val = icmp->args[0];
            break;
          }
        default:
          fmt::println("{:cd}", icmp);
          fmt::println("{:cd}", instr);
          fmt::println("POS:{}  NEG:{}", positive, negated);
          TODO("okak");
          break;
      }
      if (!new_val.is_invalid()) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_val);
        instr.destroy();
        return;
      }
    }
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::FCmp)) {
    auto fcmp = instr->args[0].as_instr();
    bool negated = false;
    bool positive = false;
    if (fcmp->args[1] == instr->args[1] && fcmp->args[0] == instr->args[2]) {
      positive = true;
    } else if (fcmp->args[1] == instr->args[2] &&
               fcmp->args[0] == instr->args[1]) {
      negated = true;
    }
    if (negated || positive) {
      fir::Builder b{instr};
      fir::ValueR new_val;
      switch ((fir::FCmpInstrSubType)fcmp->subtype) {
        case fir::FCmpInstrSubType::OGT:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::FMax
                                              : fir::IntrinsicSubType::FMin);
          break;
        case fir::FCmpInstrSubType::OEQ:
          if (positive && !fcmp->args[0].is_constant()) {
            new_val = fcmp->args[0];
            break;
          }
        case fir::FCmpInstrSubType::UEQ:
        case fir::FCmpInstrSubType::OGE:
        case fir::FCmpInstrSubType::OLT:
        case fir::FCmpInstrSubType::OLE:
        case fir::FCmpInstrSubType::UGT:
        case fir::FCmpInstrSubType::UGE:
        case fir::FCmpInstrSubType::ULT:
        case fir::FCmpInstrSubType::ULE:
        default:
          fmt::println("{:cd}", fcmp);
          fmt::println("{:cd}", instr);
          TODO("okak");
      }
      if (!new_val.is_invalid()) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_val);
        instr.destroy();
        return;
      }
    }
  }
}
void simplify_cond_branch(fir::Instr instr, fir::BasicBlock bb,
                          fir::Context & /*ctx*/, WorkList &worklist) {
  if (instr->args[0].is_constant()) {
    auto c = instr->args[0].as_constant();
    fir::Builder b(bb);
    b.at_end(bb);

    auto *target = instr->bbs.data();

    if (c->is_poison()) {
      // TODO: Could also pplace some unreach??
    } else {
      auto v1 = c->as_int();
      if (v1 == 0) {
        target = &instr->bbs[1];
      }
    }

    auto new_branch = b.build_branch(target->bb);
    for (auto old_arg : target->args) {
      new_branch.add_bb_arg(0, old_arg);
    }
    instr.remove_from_parent();
    return;
  }
  if (instr->args[0].is_instr()) {
    auto cond = instr->args[0].as_instr();
    if (cond->is(fir::InstrType::UnaryInstr) &&
        cond->subtype == (u32)fir::UnaryInstrSubType::Not) {
      TVec<fir::ValueR> args0 = {instr->bbs[0].args.begin(),
                                 instr->bbs[0].args.end()};
      TVec<fir::ValueR> args1 = {instr->bbs[1].args.begin(),
                                 instr->bbs[1].args.end()};
      fir::BasicBlock bb0 = instr->bbs[0].bb;
      fir::BasicBlock bb1 = instr->bbs[1].bb;
      instr.clear_bb_args(0);
      instr.clear_bb_args(1);

      instr.replace_bb(0, bb1, true, false);
      for (auto arg : args1) {
        instr.add_bb_arg(0, arg);
      }
      instr.replace_bb(1, bb0, true, false);
      for (auto arg : args0) {
        instr.add_bb_arg(1, arg);
      }
      instr.replace_arg(0, cond->args[0]);
      worklist.push_back({instr, instr->get_parent()});
    }
  }
}

void simplify_switch_branch(fir::Instr instr, fir::BasicBlock bb,
                            fir::Context & /*ctx*/, WorkList & /*worklist*/) {
  if (!instr->args.empty() && instr->args.back().is_constant()) {
    fir::Builder b(bb);
    b.at_end(bb);

    bool might_be_found = false;
    fir::BBRefWithArgs *target = nullptr;
    auto v1 = instr->args.back().as_constant();
    for (size_t arg_id = 0; arg_id < instr->args.size() - 1; arg_id++) {
      if (!instr->args[arg_id].is_constant()) {
        might_be_found = true;
        continue;
      }
      auto argC = instr->args[arg_id].as_constant();
      if (argC->eql(*v1.operator->())) {
        target = &instr->bbs[arg_id];
        break;
      }
    }

    if (!might_be_found && nullptr == target &&
        instr->bbs.size() == instr->args.size()) {
      // use default case then;
      target = &instr->bbs.back();
    }

    if (target != nullptr) {
      auto new_branch = b.build_branch(target->bb);
      for (auto old_arg : target->args) {
        new_branch.add_bb_arg(0, old_arg);
      }
      instr.remove_from_parent();
      return;
    }
  }

  // if theres any bb+arg that is the same as default forward it to it
  if (instr->bbs.size() == instr->args.size()) {
    fir::Builder b(bb);
    b.at_end(bb);

    for (size_t ip1 = instr->args.size() - 1; ip1 > 0; ip1--) {
      auto &default_target = instr->bbs.back();
      size_t arg_idx = ip1 - 1;
      auto &curr_bb = instr->bbs[arg_idx];
      if (default_target.bb != curr_bb.bb) {
        continue;
      }
      bool found = true;
      ASSERT(curr_bb.args.size() == default_target.args.size());
      for (size_t i = 0; i < curr_bb.args.size(); i++) {
        if (curr_bb.args[i] != default_target.args[i]) {
          found = false;
          break;
        }
      }
      if (found) {
        instr.remove_arg(arg_idx);
        instr.remove_bb(arg_idx);
      }
    }
  }
}

void simplify_extend(fir::Instr instr, fir::BasicBlock /*bb*/,
                     fir::Context &ctx, WorkList &worklist) {
  // TODO: could also maybe figure out cases where we can convert everything
  // into higher bitwidth
  (void)ctx;
  auto iszext = instr->is(fir::InstrType::ZExt);
  if (instr->args[0].get_type() == instr.get_type()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[0]);
    instr.destroy();
    return;
  }
  if (instr->args[0].is_constant()) {
    auto c = instr->args[0].as_constant()->as_int();
    push_all_uses(worklist, instr);
    if (iszext) {
      auto mask = ((u128)1 << instr->args[0].get_type()->get_bitwidth()) - 1;
      instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(
          std::bit_cast<i128>(c & mask), instr->get_type())});
    } else {
      instr->replace_all_uses(fir::ValueR{
          ctx->get_constant_value(std::bit_cast<i128>(c), instr->get_type())});
    }
    instr.destroy();
    return;
  }
  if (iszext && instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ITrunc)) {
    // a -> tr -> b
    auto trunc = instr->args[0].as_instr();
    auto input_ty = trunc->args[0].get_type();
    auto input_width = input_ty->get_bitwidth();
    auto output_type = instr.get_type();
    auto output_width = output_type->get_bitwidth();
    fir::Builder b(instr);
    auto bitwidth = trunc.get_type()->get_size() * 8;
    auto mask = ctx->get_constant_value(((u64)1 << bitwidth) - 1, input_ty);
    auto new_result = b.build_binary_op(trunc->args[0], fir::ValueR{mask},
                                        fir::BinaryInstrSubType::And);
    if (input_width == output_width) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(new_result);
    } else if (input_width < output_width) {
      auto end_res = b.build_zext(new_result, output_type);
      instr->replace_all_uses(end_res);
    } else if (input_width > output_width) {
      auto end_res = b.build_itrunc(new_result, output_type);
      instr->replace_all_uses(end_res);
    }
    instr.destroy();
    return;
  }
  if ((instr->is(fir::InstrType::SExt) || iszext) &&
      instr->args[0].is_instr()) {
    auto argi = instr->args[0].as_instr();
    if (argi->is(fir::InstrType::ZExt)) {
      // just to make sure incase that didnt get cleaned up yet
      if (argi->args[0].get_type() != argi.get_type()) {
        fir::Builder b(instr);
        auto new_result = b.build_zext(argi->args[0], instr->get_type());
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_result);
        instr.destroy();
        return;
      }
    }
    auto is_non_overflowing = (argi->NSW && !iszext) || (argi->NUW && iszext);
    if (argi->is(fir::BinaryInstrSubType::And) ||
        argi->is(fir::BinaryInstrSubType::Or) ||
        argi->is(fir::BinaryInstrSubType::Xor) ||
        (argi->is(fir::BinaryInstrSubType::IntAdd) && is_non_overflowing) ||
        (argi->is(fir::BinaryInstrSubType::IntMul) && is_non_overflowing) ||
        (iszext && argi->is(fir::BinaryInstrSubType::Shr))) {
      fir::Builder buh{argi};
      auto ext_ty = instr->get_type();
      fir::ValueR arg1Ext;
      fir::ValueR arg2Ext;
      if (iszext) {
        arg1Ext = buh.build_zext(argi->args[0], ext_ty);
        arg2Ext = buh.build_zext(argi->args[1], ext_ty);
      } else {
        arg1Ext = buh.build_sext(argi->args[0], ext_ty);
        arg2Ext = buh.build_sext(argi->args[1], ext_ty);
      }
      auto res = buh.build_binary_op(
          arg1Ext, arg2Ext, (fir::BinaryInstrSubType)argi->get_instr_subtype());
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return;
    }
  }
  if (instr->is(fir::InstrType::SExt) && instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::SExt)) {
    auto sext = instr->args[0].as_instr();
    fir::Builder b(instr);
    auto new_result = b.build_sext(sext->args[0], instr->get_type());
    push_all_uses(worklist, instr);
    instr->replace_all_uses(new_result);
    instr.destroy();
    return;
  }
}

void simplify_itrunc(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                     WorkList &worklist, AttributerManager &man) {
  (void)instr;
  (void)worklist;
  // ext
  // +-*
  // itrunc
  // = +-* on smaller bitwidth
  // since lea doesnt like 1byte value we skip those for now
  auto old_bitwidth = instr->args[0].get_type()->get_bitwidth();
  auto new_bitwidth = instr.get_type()->get_bitwidth();
  auto mask = ((i128)1 << new_bitwidth) - 1;

  if (instr->args[0].get_type() == instr.get_type()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[0]);
    instr.destroy();
    return;
  }
  if (instr->args[0].is_constant()) {
    auto c = instr->args[0].as_constant();
    if (c->is_poison()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(
          fir::ValueR{ctx->get_poisson_value(instr->get_type())});
      instr.destroy();
      return;
    }
    auto v = c->as_int();

    ASSERT(new_bitwidth < old_bitwidth);

    auto truncated_v = v & mask;
    auto new_v = ctx->get_constant_int(truncated_v, new_bitwidth);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(fir::ValueR{new_v});
    instr.destroy();
    return;
  }
  if (instr->args[0].is_bb_arg()) {
    auto arg = instr->args[0].as_bb_arg();
    auto origin_bb = arg->get_parent();
    // cant be entry basic block
    if (origin_bb->get_parent().func->basic_blocks[0] != origin_bb) {
      // fmt::println("Got hit {}", arg->get_n_uses());
      // for (auto &u : arg->uses) {
      //   fmt::println("  {:cd}", u.user);
      // }
      // if we have a bbarg as argument and its only used once
      //  it prob makes sense to just propagate the itrunc through the
      //  bb arg in the hope it optimizes better before that
      // this could also backfire (should be safe if we also propagate the
      // sext) but if the itrunc gets multiple uses it wont be propagated
      // and we got issues then
      if (arg->get_n_uses() == 1) {
        (void)bb;
        // fmt::println("{:cd}", origin_bb);
        // fmt::println("{:cd}", bb);
        auto arg_id = origin_bb->get_arg_id(arg);
        for (auto &u : origin_bb->uses) {
          auto user = u.user;
          fir::Builder buh{user};
          auto new_trunc_val = buh.build_itrunc(user->bbs[u.argId].args[arg_id],
                                                instr->get_type());
          user.replace_bb_arg(u.argId, arg_id, new_trunc_val);
          // fmt::println("{:cd}", user->get_parent());
        }
        arg->_type = instr.get_type();
        instr->replace_all_uses(fir::ValueR{arg});
        instr.destroy();
        // fmt::println("{:cd}", origin_bb);
        // fmt::println("{:cd}", bb);
        // TODO("okak");
        return;
      }
    }
  }
  if (instr->args[0].is_instr()) {
    auto arg_i = instr->args[0].as_instr();
    auto out_type = instr->get_type();
    (void)out_type;

    if ((arg_i->is(fir::InstrType::SExt) || arg_i->is(fir::InstrType::ZExt)) &&
        arg_i->args[0].get_type() == instr.get_type()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(arg_i->args[0]);
      instr.destroy();
      return;
    }

    switch (arg_i->instr_type) {
      case fir::InstrType::BinaryInstr: {
        auto i_arg0 = arg_i->args[0];
        auto i_arg1 = arg_i->args[1];
        fir::Builder b{instr};
        switch ((fir::BinaryInstrSubType)arg_i->subtype) {
          case fir::BinaryInstrSubType::Shl: {
            if (i_arg1.is_constant()) {
              auto i_arg1c = i_arg1.as_constant()->as_int();
              if (i_arg1c >= new_bitwidth) {
                push_all_uses(worklist, instr);
                instr->replace_all_uses(fir::ValueR{
                    ctx->get_constant_value((i128)0, instr->get_type())});
                instr.destroy();
                return;
              }
              if (i_arg1c >= 0 && i_arg1c < ((i128)1 << new_bitwidth)) {
                push_all_uses(worklist, instr);
                auto v0 = b.build_itrunc(i_arg0, out_type);
                auto v1 = b.build_itrunc(i_arg1, out_type);
                auto r = b.build_binary_op(
                    v0, v1, (fir::BinaryInstrSubType)arg_i->subtype);
                push_all_uses(worklist, instr);
                worklist.push_back(
                    {v0.as_instr(), v0.as_instr()->get_parent()});
                worklist.push_back(
                    {v1.as_instr(), v1.as_instr()->get_parent()});
                instr->replace_all_uses(r);
                instr.destroy();
                return;
              }
            }
          } break;
          case fir::BinaryInstrSubType::Xor:
          case fir::BinaryInstrSubType::And:
          case fir::BinaryInstrSubType::Or:
          case fir::BinaryInstrSubType::IntSub:
          case fir::BinaryInstrSubType::IntMul:
          case fir::BinaryInstrSubType::IntAdd: {
            auto v0 = b.build_itrunc(i_arg0, out_type);
            auto v1 = b.build_itrunc(i_arg1, out_type);
            auto r = b.build_binary_op(v0, v1,
                                       (fir::BinaryInstrSubType)arg_i->subtype);
            worklist.push_back({v0.as_instr(), v0.as_instr()->get_parent()});
            worklist.push_back({v1.as_instr(), v1.as_instr()->get_parent()});
            push_all_uses(worklist, r.as_instr());
            instr->replace_all_uses(r);
            instr.destroy();
            return;
          }
          default:
            break;
        }
        break;
      }
      default:
        break;
    }
  }

  auto *arg_known_bits = man.get_or_create_analysis<KnownBits>(instr->args[0]);
  if (((arg_known_bits->known_one ^ arg_known_bits->known_zero) & mask) ==
      mask) {
    auto val = ctx->get_constant_int((arg_known_bits->known_one & mask) |
                                         (arg_known_bits->known_zero & mask),
                                     new_bitwidth);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(fir::ValueR{val});
    instr.destroy();
    return;
  }
}

void simplify_unary(fir::Instr instr, fir::BasicBlock /*bb*/, fir::Context &ctx,
                    WorkList &worklist) {
  if (instr->args[0].is_constant() &&
      instr->args[0].as_constant()->is_poison()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(
        fir::ValueR{ctx->get_poisson_value(instr.get_type())});
    instr.destroy();
    return;
  }
  if ((fir::UnaryInstrSubType)instr->subtype ==
          fir::UnaryInstrSubType::IntNeg &&
      instr->get_type()->get_bitwidth() == 1) {
    instr->subtype = (u32)fir::UnaryInstrSubType::Not;
    worklist.push_back(
        InstSimplify::WorkItem{.instr = instr, .b = instr->get_parent()});
    return;
  }
  if ((fir::UnaryInstrSubType)instr->subtype == fir::UnaryInstrSubType::Not &&
      instr->args[0].is_instr()) {
    auto input_instr = instr->args[0].as_instr();
    if (input_instr->is(fir::InstrType::UnaryInstr) &&
        (fir::UnaryInstrSubType)input_instr->subtype ==
            fir::UnaryInstrSubType::Not) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(input_instr->args[0]);
      instr.destroy();
      return;
    }
    if (input_instr->is(fir::InstrType::UnaryInstr) &&
        (fir::UnaryInstrSubType)input_instr->subtype ==
            fir::UnaryInstrSubType::Not) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(input_instr->args[0]);
      instr.destroy();
      return;
    }
  }
  if ((fir::UnaryInstrSubType)instr->subtype == fir::UnaryInstrSubType::Not &&
      instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ICmp)) {
    push_all_uses(worklist, instr);
    auto old_icmp = instr->args[0].as_instr();
    auto new_subtype = fir::ICmpInstrSubType::INVALID;
    switch ((fir::ICmpInstrSubType)old_icmp->subtype) {
      case fir::ICmpInstrSubType::INVALID:
        break;
      case fir::ICmpInstrSubType::ULT:
        new_subtype = fir::ICmpInstrSubType::UGE;
        break;
      case fir::ICmpInstrSubType::SLT:
        new_subtype = fir::ICmpInstrSubType::SGE;
        break;
      case fir::ICmpInstrSubType::NE:
        new_subtype = fir::ICmpInstrSubType::EQ;
        break;
      case fir::ICmpInstrSubType::EQ:
        new_subtype = fir::ICmpInstrSubType::NE;
        break;
      case fir::ICmpInstrSubType::SGT:
        new_subtype = fir::ICmpInstrSubType::SLE;
        break;
      case fir::ICmpInstrSubType::UGT:
        new_subtype = fir::ICmpInstrSubType::ULE;
        break;
      case fir::ICmpInstrSubType::UGE:
        new_subtype = fir::ICmpInstrSubType::ULT;
        break;
      case fir::ICmpInstrSubType::ULE:
        new_subtype = fir::ICmpInstrSubType::UGT;
        break;
      case fir::ICmpInstrSubType::SGE:
        new_subtype = fir::ICmpInstrSubType::SLT;
        break;
      case fir::ICmpInstrSubType::SLE:
        new_subtype = fir::ICmpInstrSubType::SGT;
        break;
      case fir::ICmpInstrSubType::MulOverflow:
      case fir::ICmpInstrSubType::AddOverflow:
        // TODO: impl
        return;
    }
    ASSERT(new_subtype != fir::ICmpInstrSubType::INVALID);

    auto bb = fir::Builder{instr};
    auto new_comp =
        bb.build_int_cmp(old_icmp->args[0], old_icmp->args[1], new_subtype);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(new_comp);
    instr.destroy();
    return;
  }
  if ((fir::UnaryInstrSubType)instr->subtype == fir::UnaryInstrSubType::Not &&
      instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::FCmp)) {
    push_all_uses(worklist, instr);
    auto old_fcmp = instr->args[0].as_instr();
    auto new_subtype = fir::FCmpInstrSubType::INVALID;
    switch ((fir::FCmpInstrSubType)old_fcmp->subtype) {
      case fir::FCmpInstrSubType::OEQ:
        new_subtype = fir::FCmpInstrSubType::ONE;
        break;
      case fir::FCmpInstrSubType::OGT:
        new_subtype = fir::FCmpInstrSubType::OLE;
        break;
      case fir::FCmpInstrSubType::OGE:
        new_subtype = fir::FCmpInstrSubType::OLT;
        break;
      case fir::FCmpInstrSubType::OLT:
        new_subtype = fir::FCmpInstrSubType::OGE;
        break;
      case fir::FCmpInstrSubType::OLE:
        new_subtype = fir::FCmpInstrSubType::OGT;
        break;
      case fir::FCmpInstrSubType::ONE:
        new_subtype = fir::FCmpInstrSubType::OEQ;
        break;
      case fir::FCmpInstrSubType::UEQ:
        new_subtype = fir::FCmpInstrSubType::UNE;
        break;
      case fir::FCmpInstrSubType::UGT:
        new_subtype = fir::FCmpInstrSubType::ULE;
        break;
      case fir::FCmpInstrSubType::UGE:
        new_subtype = fir::FCmpInstrSubType::ULT;
        break;
      case fir::FCmpInstrSubType::ULT:
        new_subtype = fir::FCmpInstrSubType::UGE;
        break;
      case fir::FCmpInstrSubType::ULE:
        new_subtype = fir::FCmpInstrSubType::UGT;
        break;
      case fir::FCmpInstrSubType::UNE:
        new_subtype = fir::FCmpInstrSubType::UEQ;
        break;
      case fir::FCmpInstrSubType::AlwFalse:
        new_subtype = fir::FCmpInstrSubType::AlwTrue;
        break;
      case fir::FCmpInstrSubType::AlwTrue:
        new_subtype = fir::FCmpInstrSubType::AlwFalse;
        break;
      case fir::FCmpInstrSubType::ORD:
      case fir::FCmpInstrSubType::UNO:
      case fir::FCmpInstrSubType::IsNaN:
      case fir::FCmpInstrSubType::INVALID:
        break;
    }
    ASSERT(new_subtype != fir::FCmpInstrSubType::INVALID);

    auto bb = fir::Builder{instr};
    auto new_comp =
        bb.build_float_cmp(old_fcmp->args[0], old_fcmp->args[1], new_subtype);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(new_comp);
    instr.destroy();
    return;
  }
  if (!instr->args[0].is_constant()) {
    return;
  }
  if ((fir::UnaryInstrSubType)instr->subtype == fir::UnaryInstrSubType::Not) {
    push_all_uses(worklist, instr);
    auto out_type = instr.get_type();
    ASSERT(out_type->is_int());
    auto mask = ((i128)1 << out_type->as_int()) - 1;
    push_all_uses(worklist, instr);
    instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(
        (~instr->args[0].as_constant()->as_int()) & mask, out_type)});
    instr.destroy();
    return;
  }
}

void simplify_conversion(fir::Instr instr, fir::BasicBlock /*bb*/,
                         fir::Context &ctx, WorkList &worklist) {
  (void)ctx;
  (void)worklist;
  switch ((fir::ConversionSubType)instr->subtype) {
    case fir::ConversionSubType::INVALID:
      TODO("unreach");
    case fir::ConversionSubType::BitCast:
      if (instr->args[0].is_instr()) {
        auto iarg0 = instr->args[0].as_instr();
        if (iarg0->is(fir::InstrType::Conversion) &&
            iarg0->subtype == (u32)fir::ConversionSubType::BitCast &&
            iarg0->args[0].get_type() == instr.get_type()) {
          push_all_uses(worklist, instr);
          instr->replace_all_uses(iarg0->args[0]);
          instr.destroy();
          return;
        }
      }
      if (instr->args[0].is_constant()) {
        fmt::println("implc const bitcast instr {:cd}", instr);
        return;
        // TODO("todo");
      }
      break;
    case fir::ConversionSubType::SITOFP:
    case fir::ConversionSubType::UITOFP:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_int()) {
        auto val = instr->args[0].as_constant()->as_int();
        push_all_uses(worklist, instr);
        auto out_width = instr->get_type()->as_float();
        if (out_width == 32) {
          instr->replace_all_uses(fir::ValueR{
              ctx->get_constant_value((f32)val, instr->get_type())});
        } else if (out_width == 64) {
          instr->replace_all_uses(fir::ValueR{
              ctx->get_constant_value((f64)val, instr->get_type())});
        } else {
          TODO("Not supported other float bitwidths");
        }
        instr.destroy();
        return;
      }
      break;
    case fir::ConversionSubType::PtrToInt:
    case fir::ConversionSubType::IntToPtr:
      if (instr->args[0].is_constant()) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return;
      }
      break;
    case fir::ConversionSubType::FPTOSI:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_float()) {
        auto val = instr->args[0].as_constant()->as_float();
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value((i128)val, instr->get_type())});
        instr.destroy();
        // TODO("OKAK IMPL");
        return;
      }
      break;
    case fir::ConversionSubType::FPTOUI:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_float()) {
        //   auto val = instr->args[0].as_constant()->as_float();
        //   push_all_uses(worklist, instr);
        //   instr->replace_all_uses(
        //       fir::ValueR{ctx->get_constant_value((u64)val,
        //       instr->get_type())});
        //   instr.destroy();
        TODO("OKAK IMPL");
        return;
      }
      break;
    case fir::ConversionSubType::FPEXT:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_float()) {
        ASSERT(instr->args[0].get_type()->as_float() == 32);
        ASSERT(instr.get_type()->as_float() == 64);
        auto val = instr->args[0].as_constant()->as_f32();
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value((f64)val, instr->get_type())});
        instr.destroy();
        return;
      }
      break;
    case fir::ConversionSubType::FPTRUNC:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_float()) {
        ASSERT(instr->args[0].get_type()->as_float() == 64);
        ASSERT(instr.get_type()->as_float() == 32);
        auto val = instr->args[0].as_constant()->as_f64();
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value((f32)val, instr->get_type())});
        instr.destroy();
        return;
      }
      break;
  }
}

bool simplify_store(fir::Instr instr) {
  if ((instr->args[0].is_constant() &&
       instr->args[0].as_constant()->is_poison()) ||
      (instr->args[1].is_constant() &&
       instr->args[1].as_constant()->is_poison())) {
    instr.destroy();
    return true;
  }
  return false;
}

void simplify_call(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                   WorkList &worklist) {
  (void)bb;
  (void)ctx;
  (void)worklist;
  if (instr->args[0].is_constant() && instr->args[0].as_constant()->is_func()) {
    auto funci = instr->args[0].as_constant()->as_func();
    if (instr->get_n_uses() == 0 &&
        (funci->mem_read_none || funci->mem_read_only)) {
      instr.destroy();
      return;
    }

    if (foptim::utils::assume_cstdlib_beheaviour) {
      // constant propagate strlen of string literals
      if (funci->name == "strlen" && instr->args[1].is_constant() &&
          instr->args[1].as_constant()->is_global()) {
        auto g = instr->args[1].as_constant()->as_global();
        if (g->is_constant && g->init_value != nullptr) {
          auto len = strlen((const char *)g->init_value);
          auto len_constant = ctx->get_constant_int(len, 64);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(fir::ValueR{len_constant});
          instr.destroy();
          return;
        }
      }
      // NOTE: this current version does not work cause it does not append
      //  printf(lit) -> fputs(lit, stdout)
      //  if (funci->name == "printf" && instr->args.size() == 2 &&
      //      instr->get_n_uses() == 0) {

      //   if (!ctx->has_function("write")) {
      //     auto puts_func = ctx->create_function(
      //         "write",
      //         ctx->get_func_ty(ctx->get_int_type(64),
      //                          {ctx->get_int_type(32),
      //                          ctx->get_ptr_type(),
      //                           ctx->get_int_type(64)}));
      //     puts_func->linkage = fir::Linkage::External;
      //     puts_func->basic_blocks.clear();
      //   }
      //   if (!ctx->has_function("strlen")) {
      //     auto strlen_func = ctx->create_function(
      //         "strlen",
      //         ctx->get_func_ty(ctx->get_int_type(64),
      //         {ctx->get_ptr_type()}));
      //     strlen_func->linkage = fir::Linkage::External;
      //     strlen_func->basic_blocks.clear();
      //   }

      //   // fir::ValueR stdout{};
      //   // if (!ctx->has_global("stdout")) {
      //   //   auto global = ctx->insert_global("stdout", 8);
      //   //   global->linkage = fir::Linkage::External;
      //   //   stdout = fir::ValueR{ctx->get_constant_value(global)};
      //   // } else {
      //   //   stdout =
      //   //
      //   fir::ValueR{ctx->get_constant_value(ctx->get_global("stdout"))};
      //   // }

      //   auto write_func = ctx->get_function("write");
      //   auto strlen_func = ctx->get_function("strlen");

      //   fir::Builder builder{instr};
      //   // TODO: should load stdcout
      //   //
      //   //  auto stdout_ptr_val = builder.build_load(ctx->get_ptr_type(),
      //   //  stdout); auto stdout_val =
      //   //      builder.build_load(ctx->get_ptr_type(), stdout_ptr_val);
      //   //  fir::ValueR args[2] = {instr->args[1],
      //   fir::ValueR{stdout_val}}; fir::ValueR args1[1] = {
      //       instr->args[1],
      //   };
      //   auto string_len = builder.build_call(
      //       fir::ValueR{ctx->get_constant_value(strlen_func)},
      //       write_func->func_ty, ctx->get_int_type(32), args1);
      //   fir::ValueR args2[3] = {
      //       fir::ValueR{ctx->get_constant_int(1, 32)},
      //       instr->args[1],
      //       string_len,
      //   };
      //   builder.build_call(fir::ValueR{ctx->get_constant_value(write_func)},
      //                      write_func->func_ty, ctx->get_int_type(32),
      //                      args2);
      //   instr.destroy();
      //   // TODO("okak");
      //   return;
      // }
    }
  }
}

void simplify_load(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                   WorkList &worklist) {
  (void)bb;
  (void)ctx;
  (void)worklist;
  if (instr->args[0].is_constant()) {
    auto arg0_const = instr->args[0].as_constant();
    if (arg0_const->is_poison()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(
          fir::ValueR{ctx->get_poisson_value(instr->get_type())});
      instr.destroy();
      return;
    }
    if (arg0_const->is_null() ||
        (arg0_const->is_int() && arg0_const->as_int() == 0)) {
      push_all_uses(worklist, instr);
      // TODO: in theory could just put an unreach here
      instr->replace_all_uses(
          fir::ValueR{ctx->get_poisson_value(instr->get_type())});
      return;
    }
    if (arg0_const->is_global() && arg0_const->as_global()->is_constant) {
      // TOOD: can fix this i guess
      auto glob = arg0_const->as_global();
      if (!glob->reloc_info.empty()) {
        fmt::println("HIT");
        TODO("okak implement global constant loading");
        // push_all_uses(worklist, instr);
        // instr->replace_all_uses(
        //     fir::ValueR{ctx->get_poisson_value(instr->get_type())});
        // instr.destroy();
        // return;
      }
    }
  }
  // handle constant global + offset
  if (instr->args[0].is_instr()) {
    auto arg_instr = instr->args[0].as_instr();
    if (arg_instr->is(fir::InstrType::BinaryInstr) &&
        arg_instr->subtype == (u32)fir::BinaryInstrSubType::IntAdd &&
        arg_instr->args[0].is_constant() && arg_instr->args[1].is_constant()) {
      auto base_global = arg_instr->args[0].as_constant();
      auto offset = arg_instr->args[1].as_constant();
      if (base_global->is_global() && offset->is_int()) {
        auto offset_v = offset->as_int();
        auto global_v = base_global->as_global();
        for (auto reloc : global_v->reloc_info) {
          if (reloc.insert_offset == offset_v) {
            push_all_uses(worklist, instr);
            instr->replace_all_uses(fir::ValueR{reloc.ref});
            instr.destroy();
            return;
          }
        }
      }
    }
  }
}

void simplify_intrinsic(fir::Instr instr, fir::BasicBlock /*bb*/,
                        fir::Context &ctx, WorkList &worklist,
                        AttributerManager &man) {
  auto sub_type = (fir::IntrinsicSubType)instr->subtype;
  if (sub_type == fir::IntrinsicSubType::Abs && instr->args[0].is_constant()) {
    push_all_uses(worklist, instr);
    auto val = std::abs(instr->args[0].as_constant()->as_int());
    instr->replace_all_uses(
        fir::ValueR{ctx->get_constant_value(val, instr->get_type())});
    instr.destroy();
    return;
  }
  if (sub_type == fir::IntrinsicSubType::UMin ||
      sub_type == fir::IntrinsicSubType::UMax ||
      sub_type == fir::IntrinsicSubType::SMin ||
      sub_type == fir::IntrinsicSubType::SMax ||
      sub_type == fir::IntrinsicSubType::FMin ||
      sub_type == fir::IntrinsicSubType::FMax) {
    if (instr->args[0].is_constant() && instr->args[1].is_constant()) {
      push_all_uses(worklist, instr);
      switch (sub_type) {
        default:
          TODO("UNREACH");
        case fir::IntrinsicSubType::SMax: {
          push_all_uses(worklist, instr);
          auto val = std::max(instr->args[0].as_constant()->as_int(),
                              instr->args[1].as_constant()->as_int());
          instr->replace_all_uses(
              fir::ValueR{ctx->get_constant_value(val, instr->get_type())});
          instr.destroy();
          return;
        }
        case fir::IntrinsicSubType::FMin:
        case fir::IntrinsicSubType::FMax:
        case fir::IntrinsicSubType::UMin:
        case fir::IntrinsicSubType::UMax:
        case fir::IntrinsicSubType::SMin:
          fmt::println("{}", instr);
          TODO("impl");
      }
      return;
    }
    if (instr->args[0].is_constant() && !instr->args[1].is_constant()) {
      auto old0 = instr->args[0];
      auto old1 = instr->args[1];
      instr.replace_arg(0, old1);
      instr.replace_arg(1, old0);
      return;
    }
  }
  if (sub_type == fir::IntrinsicSubType::FAbs && instr->args[0].is_constant()) {
    auto ty = instr->get_type();
    auto width = ty->as_float();

    push_all_uses(worklist, instr);
    if (width == 32) {
      auto val = std::fabs(instr->args[0].as_constant()->as_f32());
      instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(val, ty)});
    } else if (width == 64) {
      auto val = std::fabs(instr->args[0].as_constant()->as_f64());
      instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(val, ty)});
    } else {
      TODO("IMPL");
    }
    instr.destroy();
    return;
  }

  switch (sub_type) {
    default:
      break;
    case fir::IntrinsicSubType::UMin:
    case fir::IntrinsicSubType::UMax:
    case fir::IntrinsicSubType::SMin:
    case fir::IntrinsicSubType::FMin:
    case fir::IntrinsicSubType::FMax:
    case fir::IntrinsicSubType::SMax: {
      // IDK if these are worth the effort
      break;
    }

    case fir::IntrinsicSubType::Abs: {
      auto *a0 = man.get_or_create_analysis<KnownBits>(instr->args[0]);
      man.run(ctx);
      auto r = a0->msb_info();
      if (r == KnownBits::KnownZero) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return;
      }
      if (r == KnownBits::KnownOne) {
        push_all_uses(worklist, instr);
        fir::Builder b{instr};
        auto negated_val =
            b.build_unary_op(instr->args[0], fir::UnaryInstrSubType::IntNeg);
        instr->replace_all_uses(negated_val);
        instr.destroy();
        return;
      }
      break;
    }
    case fir::IntrinsicSubType::FAbs:
      break;
  }
}

fir::ValueR propagate_load_through_select(fir::Instr select) {
  ASSERT(select->is(fir::InstrType::SelectInstr));
  ASSERT(select->get_n_uses() == 1);
  ASSERT(select->uses[0].user->is(fir::InstrType::LoadInstr));
  auto load = select->uses[0];
  fir::Builder bu{select};
  auto n_type = load.user.get_type();
  auto a = bu.build_load(n_type, select->args[1]);
  auto b = bu.build_load(n_type, select->args[2]);
  auto r = bu.build_select(n_type, select->args[0], a, b);
  load.user->replace_all_uses(r);
  return r;
}

void simplify_alloca(fir::Instr instr, fir::BasicBlock /*bb*/,
                     fir::Context &ctx, WorkList &worklist, AliasAnalyis &aa) {
  {
    // check if only read or written
    //  then we can delete
    TSet<fir::Use> visited;
    TVec<fir::Use> alloca_worklist{instr->uses.begin(), instr->uses.end()};
    bool is_read = false;
    bool is_written = false;
    bool escapes = false;
    TVec<fir::Use> mem2reg_blockers{};
    while (!alloca_worklist.empty()) {
      if (escapes) {
        break;
      }
      auto curr = alloca_worklist.back();
      alloca_worklist.pop_back();
      switch (curr.type) {
        case fir::UseType::NormalArg:
          if (curr.user->is(fir::InstrType::LoadInstr) && curr.argId == 0) {
            is_read |= curr.user->get_n_uses() > 0;
          } else if (curr.user->is(fir::InstrType::StoreInstr) &&
                     curr.argId == 0) {
            is_written = true;
          } else if ((curr.user->is(fir::InstrType::VectorInstr)) ||
                     (curr.user->is(fir::InstrType::BinaryInstr)) ||
                     (curr.user->is(fir::InstrType::SelectInstr) &&
                      curr.argId != 0)) {
            if (curr.user->is(fir::InstrType::SelectInstr)) {
              // binary_instrs we generally can fix by running sroa
              mem2reg_blockers.push_back(curr);
            }
            alloca_worklist.insert(alloca_worklist.end(),
                                   curr.user->uses.begin(),
                                   curr.user->uses.end());
          } else if ((curr.user->is(fir::InstrType::StoreInstr) &&
                      curr.argId == 1) ||
                     curr.user->is(fir::InstrType::CallInstr) ||
                     curr.user->is(fir::InstrType::Intrinsic) ||
                     curr.user->is(fir::InstrType::ICmp)) {
            mem2reg_blockers.push_back(curr);
            escapes = true;
          } else {
            fmt::println("{}", instr);
            fmt::println("{}", curr.user);
            TODO("IMPL");
          }
          break;
        case fir::UseType::BBArg: {
          mem2reg_blockers.push_back(curr);
          const auto &bb_uses =
              curr.user->bbs[curr.argId].bb->args[curr.bbArgId]->uses;
          for (auto bb_use : bb_uses) {
            if (visited.contains(bb_use)) {
              continue;
            }
            visited.insert(bb_use);
            alloca_worklist.push_back(bb_use);
          }
        } break;
        case fir::UseType::BB:
          fmt::println("{}", instr);
          TODO("IMPL");
          break;
      }
    }

    // if alloca is only read then all reads are poision
    // if alloca is only written then we can discard
    if (escapes) {
      return;
    }
    if ((!is_written && is_read) || (is_written && !is_read)) {
      push_all_uses(worklist, instr);
      TVec<fir::Use> use_copy{instr->uses.begin(), instr->uses.end()};
      auto p_val = fir::ValueR{ctx->get_poisson_value(ctx->get_ptr_type())};
      for (auto u : use_copy) {
        if (u.user->is(fir::InstrType::LoadInstr)) {
          push_all_uses(worklist, u.user);
          u.user->replace_all_uses(p_val);
          u.user.destroy();
          continue;
        }
        if (u.user->is(fir::InstrType::StoreInstr) && u.argId == 0) {
          u.user.destroy();
          continue;
        }
        u.replace_use(p_val);
      }
      instr.destroy();
      return;
    }

    // if the mem2reg blockers all can be transformed we should definetly do
    // it only makes sense if its static and not too big
    if (!mem2reg_blockers.empty() && instr->args[0].is_constant() &&
        instr->args[0].as_constant()->as_int() < 64) {
      // fmt::println("{}", *instr->get_parent()->get_parent().func);
      // fmt::println("{}", instr);
      bool transform = true;
      for (auto b : mem2reg_blockers) {
        // fmt::println("{}", b);
        if (b.user->is(fir::InstrType::SelectInstr)) {
          if (b.user->get_n_uses() == 1 &&
              b.user->uses[0].user->is(fir::InstrType::LoadInstr) &&
              ((b.argId == 1 && aa.is_known_local_stack(b.user->args[2])) ||
               (b.argId == 2 && aa.is_known_local_stack(b.user->args[1])))) {
            continue;
          }
        } else if (b.type == fir::UseType::BBArg) {
          transform = false;
          continue;
        } else {
          fmt::println("{} {}", b.user, b.user->get_parent());
          TODO("okak");
        }
        transform = false;
      }
      if (transform) {
        for (auto b : mem2reg_blockers) {
          if (b.user->is(fir::InstrType::SelectInstr)) {
            auto load = b.user->uses[0].user;
            push_all_uses(worklist, load);
            propagate_load_through_select(b.user);
            // load.destroy();
          }
        }
        // fmt::println("{}", *instr->get_parent()->get_parent().func);
        // TODO("okey");
      }
      // fmt::println("=======================");
    }
  }
}

void simplify_ext_byte_vector(fir::Instr instr, fir::Context &ctx,
                              WorkList &worklist, u32 extend_to,
                              u32 n_out_elems, fir::TypeR out_type) {
  if (instr->is(fir::InstrType::StoreInstr)) {
    auto out_type = ctx->get_int_type(extend_to * 8);
    fir::Builder bb{instr};
    auto in_addr = instr->args[0];
    auto in_val = instr->args[1];
    auto casted_val = bb.build_conversion_op(
        in_val,
        n_out_elems == 1 ? out_type : ctx->get_vec_type(out_type, n_out_elems),
        fir::ConversionSubType::BitCast);
    auto res = bb.build_store(in_addr, casted_val);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(res);
    instr.destroy();
    return;
  }
  if (instr->is(fir::InstrType::VectorInstr) &&
      instr->subtype == (u32)fir::VectorISubType::Broadcast) {
    fir::Builder bb{instr};
    auto in_val = instr->args[0];
    auto orig_v = bb.build_zext(in_val, out_type);
    auto v = orig_v;
    for (u32 i = 1; i < extend_to; i++) {
      auto add = bb.build_binary_op(
          in_val, fir::ValueR{ctx->get_constant_value(i * 8, out_type)},
          fir::BinaryInstrSubType::Shl);
      v = bb.build_binary_op(v, add, fir::BinaryInstrSubType::Or);
    }
    if (n_out_elems != 1) {
      v = bb.build_vbroadcast(v, ctx->get_vec_type(out_type, n_out_elems));
    }
    auto res = bb.build_conversion_op(v, instr->get_type(),
                                      fir::ConversionSubType::BitCast);
    worklist.push_back({orig_v.as_instr(), orig_v.as_instr()->get_parent()});
    push_all_uses(worklist, instr);
    instr->replace_all_uses(res);
    instr.destroy();
    return;
  }
}
void simplify_vector(fir::Instr instr, fir::BasicBlock /*bb*/,
                     fir::Context &ctx, WorkList &worklist) {
  const auto &v_t = instr->get_type()->as_vec();
  if (v_t.type == fir::VectorType::SubType::Integer && v_t.bitwidth == 8) {
    auto extend_to = v_t.member_number;
    if ((extend_to % 8 == 0 && extend_to / 8 <= 2) ||
        (extend_to % 4 == 0 && extend_to / 4 <= 4)) {
      auto n_out_elems = extend_to / 4;
      if (extend_to == 8) {
        n_out_elems = extend_to / 8;
      }
      auto out_type = ctx->get_int_type(extend_to * 8);
      simplify_ext_byte_vector(instr, ctx, worklist, extend_to, n_out_elems,
                               out_type);
    }
  }
}

void simplify_extract(fir::Instr instr, WorkList &worklist) {
  if (instr->args[0].is_instr()) {
    auto argi = instr->args[0].as_instr();
    if (argi->is(fir::InstrType::InsertValue)) {
      if (instr->args[1].eql(argi->args[2])) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(argi->args[1]);
        instr.destroy();
        return;
      }
      fir::Builder bb{instr};
      fir::ValueR v[1] = {instr->args[1]};
      auto res = bb.build_extract_value(argi->args[0], v, instr->get_type());
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return;
    }
  }
}

void simplify(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
              WorkList &worklist, AttributerManager &man, AliasAnalyis &anal) {
  using namespace foptim::fir;
  (void)anal;
  auto instr_ty = instr->get_instr_type();
  man.reset();
  if (instr_ty == InstrType::BinaryInstr) {
    simplify_binary(instr, bb, ctx, worklist, man);
    return;
  }
  if (instr_ty == InstrType::UnaryInstr) {
    simplify_unary(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::ICmp) {
    simplify_icmp(instr, bb, ctx, worklist, man);
    return;
  }
  if (instr_ty == InstrType::StoreInstr) {
    if (simplify_store(instr)) {
      return;
    }
  }
  if (instr_ty == InstrType::VectorInstr ||
      (instr_ty == InstrType::LoadInstr && instr->get_type()->is_vec()) ||
      (instr_ty == InstrType::StoreInstr && instr->get_type()->is_vec())) {
    simplify_vector(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::FCmp) {
    simplify_fcmp(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::SelectInstr) {
    simplify_select(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::CondBranchInstr) {
    simplify_cond_branch(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::SwitchInstr) {
    simplify_switch_branch(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::SExt || instr_ty == InstrType::ZExt) {
    simplify_extend(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::ITrunc) {
    simplify_itrunc(instr, bb, ctx, worklist, man);
    return;
  }
  if (instr_ty == InstrType::Conversion) {
    simplify_conversion(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::LoadInstr) {
    simplify_load(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::CallInstr) {
    simplify_call(instr, bb, ctx, worklist);
    return;
  }
  if (instr_ty == InstrType::Intrinsic) {
    simplify_intrinsic(instr, bb, ctx, worklist, man);
    return;
  }
  if (instr_ty == InstrType::ExtractValue) {
    simplify_extract(instr, worklist);
    return;
  }
  if (instr_ty == InstrType::AllocaInstr) {
    simplify_alloca(instr, bb, ctx, worklist, anal);
    return;
  }
}
}  // namespace

void InstSimplify::apply(fir::Context &ctx, fir::Function &func) {
  using namespace foptim::fir;
  AttributerManager man;
  AliasAnalyis anal{};

  // TODO: maybe replace with actual queue
  TVec<WorkItem> worklist;
  for (BasicBlock bb : func.basic_blocks) {
    auto &instrs = bb->get_instrs();
    for (auto &instr : instrs) {
      worklist.emplace_back(instr, bb);
    }
  }

  while (!worklist.empty()) {
    auto [instr, bb] = worklist.back();
    worklist.pop_back();
    if (!instr.is_valid() || !instr->parent.is_valid()) {
      continue;
    }
    simplify(instr, bb, ctx, worklist, man, anal);
  }
}

}  // namespace foptim::optim
