
#pragma once
#include <algorithm>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "optim/analysis/attributer/KnownBits.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim::InstSimp {
namespace {

bool simplify_reduction(fir::Instr instr, fir::BasicBlock /*bb*/,
                        fir::Context &ctx, WorkList &worklist,
                        AttributerManager &man) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyRed");
  }
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
  u32 n_const = 0;
  i128 constval = neutral_val;
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
        if (c.is_constant() && c.as_constant()->is_int()) {
          n_const++;
          constval += c.as_constant()->as_int();
        } else {
          red_args.push_back(c);
        }
      }
    } else {
      if (c.is_constant() && c.as_constant()->is_int()) {
        n_const++;
        constval += c.as_constant()->as_int();
      } else {
        red_args.push_back(c);
      }
    }
  }
  if (red_args.size() <= 3 || n_const < 2) {
    return false;
  }
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
    size_t max_group_size = 0;
    for (size_t i = 0; i < red_args.size(); i++) {
      // if (red_args[i].is_constant() && red_args[i].as_constant()->is_int()) {
      //   n_const++;
      //   constval += red_args[i].as_constant()->as_int();
      // } else {
      size_t endgroup = i + 1;
      for (size_t i2 = i + 1; i2 < red_args.size(); i2++) {
        if (red_args[i] != red_args[i2]) {
          endgroup = i2;
          break;
        }
      }
      max_group_size = std::max(max_group_size, endgroup - i);
      // }
    }
    if (max_group_size <= 3) {
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

void promote_constant_type(fir::Context &ctx, fir::Instr instr, u32 arg_id,
                           fir::TypeR target_ty) {
  ASSERT(instr->args[arg_id].is_constant());
  auto target_arg = instr->args[arg_id].as_constant();
  switch (target_arg->ty) {
    case fir::ConstantType::PoisonValue:
      instr->args[arg_id] = fir::ValueR{ctx->get_poisson_value(target_ty)};
      return;
    case fir::ConstantType::IntValue:
      if (target_ty->is_int() || target_ty->is_ptr()) {
        instr->args[arg_id] = fir::ValueR{
            ctx->get_constant_value(target_arg->as_int(), target_ty)};
      } else if (target_ty->is_float()) {
        auto width = target_ty->as_float();
        if (width == 32) {
          instr->args[arg_id] = fir::ValueR{ctx->get_constant_value(
              std::bit_cast<f32>(
                  (u32)std::bit_cast<u128>(target_arg->as_int())),
              target_ty)};
        } else if (width == 64) {
          instr->args[arg_id] = fir::ValueR{ctx->get_constant_value(
              std::bit_cast<f64>(
                  (u64)std::bit_cast<u128>(target_arg->as_int())),
              target_ty)};
        } else {
          TODO("UNREACH?");
        }
      }
      return;
    case fir::ConstantType::FloatValue:
      ASSERT(target_ty->is_float());
      instr->args[arg_id] = fir::ValueR{
          ctx->get_constant_value(target_arg->as_float(), target_ty)};
      return;
    case fir::ConstantType::NullPtr:
    case fir::ConstantType::GlobalPtr:
    case fir::ConstantType::FuncPtr:
    case fir::ConstantType::ConstantStruct:
      return;
    case fir::ConstantType::VectorValue:
      fmt::println("{:cd}  -> {:cd}", instr, target_ty);
      TODO("okak");
      break;
  }
}
}  // namespace

inline bool simplify_binary(fir::Instr instr, fir::BasicBlock bb,
                            fir::Context &ctx, WorkList &worklist,
                            AttributerManager &man) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyBinary");
  }
  using namespace foptim::fir;
  // since both being constant would be handleded by constant folding we
  // just asume theres one and normalzie by putting it into the secodn arg
  {
    auto swap_const_back = instr->args[0].is_constant() &&
                           (!instr->args[0].as_constant()->is_global() &&
                            !instr->args[0].as_constant()->is_func());
    auto swap_bb_arg_back =
        instr->args[0].is_bb_arg() &&
        (!instr->args[1].is_constant() && !instr->args[1].is_bb_arg());
    if (instr->is_commutative() && (swap_const_back || swap_bb_arg_back)) {
      swap_args(instr, 0, 1);
    }
  }
  if (instr->args[1].is_constant() &&
      instr->args[0].get_type() != instr->args[1].get_type()) {
    auto t1 = instr->args[0].get_type();
    auto t2 = instr->args[1].get_type();
    // skip converting integer constants to ptr type instead extend to same
    // sized integer
    if (t1->is_ptr() && t2->is_int() &&
        t1->get_bitwidth() != t2->get_bitwidth()) {
      promote_constant_type(ctx, instr, 1,
                            ctx->get_int_type(t1->get_bitwidth()));
    } else if (t1->is_vec() && t2->is_vec()) {
      auto v1 = t1->as_vec();
      auto v2 = t2->as_vec();
      if (v1.member_number == v2.member_number && v1.bitwidth > v2.bitwidth) {
        promote_constant_type(ctx, instr, 1,
                              ctx->get_int_type(t1->get_bitwidth()));
      } else if (v1.member_number == v2.member_number &&
                 v1.bitwidth == v2.bitwidth && v1.type != v2.type) {
        TODO("impl");
      }
    } else {
      promote_constant_type(ctx, instr, 1, instr->args[0].get_type());
    }
  }

  // CONSTANT EVAL
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
      return true;
    }
    // if (((c0_val != nullptr) && c0_val->is_null()) ||
    //     ((c1_val != nullptr) && c1_val->is_null())) {
    //   auto index = ((c0_val != nullptr) && c0_val->is_null()) ? 1 : 0;
    //   push_all_uses(worklist, instr);
    //   instr->replace_all_uses(instr->args[index]);
    //   instr.destroy();
    //   return true;
    // }

    if ((c0_val != nullptr) && (c1_val != nullptr)) {
      // fmt::println("{:cd}", instr);
      if (c1_val->is_int() && c0_val->is_int()) {
        // TODO: this is annoying but idk how to handle it better
        push_all_uses(worklist, instr);
        if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                     c0_val->as_int(), c1_val->as_int(),
                                     c1_val->type, ctx)) {
          instr.destroy();
          return true;
        }
      } else if (c1_val->type->is_float() && c1_val->type->as_float() == 32 &&
                 c0_val->type->is_float() && c0_val->type->as_float() == 32) {
        // TODO: this is annoying but idk how to handle it better
        push_all_uses(worklist, instr);
        if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                     c0_val->as_f32(), c1_val->as_f32(),
                                     c1_val->type, ctx)) {
          instr.destroy();
          return true;
        }
      } else if (c1_val->type->is_float() && c1_val->type->as_float() == 64 &&
                 c0_val->type->is_float() && c0_val->type->as_float() == 64) {
        // TODO: this is annoying but idk how to handle it better
        push_all_uses(worklist, instr);
        if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                     c0_val->as_f64(), c1_val->as_f64(),
                                     c1_val->type, ctx)) {
          instr.destroy();
          return true;
        }
      }
    }
  }
  if (instr->is(BinaryInstrSubType::IntAdd) && instr->args[0].is_instr()) {
    auto inner = instr->args[0].as_instr();
    if (inner->is(BinaryInstrSubType::IntAdd) &&
        inner->args[1] == instr->args[1]) {
      fir::Builder b(instr);
      auto c2 = fir::ValueR{
          ctx->get_constant_int(2, inner->get_type()->get_bitwidth())};
      auto a1 =
          b.build_binary_op(c2, inner->args[1], BinaryInstrSubType::IntMul);
      auto a1i = a1.as_instr();
      a1i->NUW = instr->NUW && inner->NUW;
      a1i->NSW = instr->NSW && inner->NSW;
      auto res =
          b.build_binary_op(inner->args[0], a1, BinaryInstrSubType::IntAdd);
      auto resi = res.as_instr();
      resi->NUW = instr->NUW && inner->NUW;
      resi->NSW = instr->NSW && inner->NSW;
      worklist.push_back({a1.as_instr(), a1.as_instr()->parent});
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      ASSERT(instr->get_type()->get_bitwidth() ==
             res.get_type()->get_bitwidth());
      instr.destroy();
      return true;
    }
    if (inner->is(BinaryInstrSubType::IntAdd) &&
        inner->args[0] == instr->args[1]) {
      fir::Builder b(instr);
      auto c2 = fir::ValueR{
          ctx->get_constant_int(2, inner->get_type()->get_bitwidth())};
      auto a1 =
          b.build_binary_op(c2, inner->args[0], BinaryInstrSubType::IntMul);
      auto a1i = a1.as_instr();
      a1i->NUW = instr->NUW && inner->NUW;
      a1i->NSW = instr->NSW && inner->NSW;
      auto res =
          b.build_binary_op(inner->args[1], a1, BinaryInstrSubType::IntAdd);
      auto resi = res.as_instr();
      resi->NUW = instr->NUW && inner->NUW;
      resi->NSW = instr->NSW && inner->NSW;
      worklist.push_back({a1.as_instr(), a1.as_instr()->parent});
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return true;
    }
    if (inner->is(BinaryInstrSubType::IntMul) &&
        inner->args[1].is_constant_int() && inner->args[0] == instr->args[1]) {
      fir::Builder b(instr);
      auto c2 = fir::ValueR{
          ctx->get_constant_int(inner->args[1].as_constant()->as_int() + 1,
                                inner->get_type()->get_bitwidth())};
      auto res =
          b.build_binary_op(inner->args[0], c2, BinaryInstrSubType::IntMul);
      worklist.push_back({res.as_instr(), res.as_instr()->parent});
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return true;
    }
  }

  if (has_const) {
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
          auto a1 =
              b.build_binary_op(const1, const2, BinaryInstrSubType::IntMul);
          auto res = b.build_binary_op(x, a1, BinaryInstrSubType::IntMul);
          worklist.emplace_back(a1.as_instr(), bb);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
        } else if (instr->subtype == (u32)BinaryInstrSubType::IntSDiv) {
          auto a1 =
              b.build_binary_op(const1, const2, BinaryInstrSubType::IntMul);
          auto res = b.build_binary_op(x, a1, BinaryInstrSubType::IntSDiv);
          worklist.emplace_back(a1.as_instr(), bb);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
        } else if (instr->subtype == (u32)BinaryInstrSubType::IntUDiv) {
          auto a1 =
              b.build_binary_op(const1, const2, BinaryInstrSubType::IntMul);
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
        return true;
      }
    }
    // (x +- constant1) +- constant2
    // x +- (constant1 +- constant2)
    // and
    // (x +- constant1) +- y
    // x +- y) +- constant1
    if (instr->subtype == (u32)BinaryInstrSubType::IntAdd &&
        instr->args[0] == instr->args[1]) {
      fir::Builder b(instr);
      auto res = b.build_int_mul(instr->args[0],
                                 fir::ValueR{ctx->get_constant_int(
                                     2, instr->get_type()->get_bitwidth())});
      worklist.emplace_back(res.as_instr(), bb);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return true;
    }
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
            return true;
          }
        }
        if (instr->args[1].is_instr()) {
          auto inner_add_sub = instr->args[1].as_instr();
          auto const2 = instr->args[0];
          if (f(inner_add_sub, const2)) {
            return true;
          }
        }
      }
    }
    // (x +- constant1) * constant2
    // (x*constant2 +- constant1*constant2)
    {
      if (instr->is(BinaryInstrSubType::IntMul) &&
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
          return true;
        }
      }
      if ((instr->is(BinaryInstrSubType::And) ||
           instr->is(BinaryInstrSubType::Or)) &&
          instr->args[0].is_instr() && instr->args[1].is_instr()) {
        bool is_base_and = instr->is(BinaryInstrSubType::And);
        auto inner1 = instr->args[0].as_instr();
        auto inner2 = instr->args[1].as_instr();
        if (((!is_base_and && inner1->is(BinaryInstrSubType::And) &&
              inner2->is(BinaryInstrSubType::And)) ||
             (is_base_and && inner1->is(BinaryInstrSubType::Or) &&
              inner2->is(BinaryInstrSubType::Or))) &&
            ((inner1->args[1].is_constant() && inner2->args[1].is_constant()) ||
             inner1->args[0] == inner2->args[0] ||
             inner1->args[1] == inner2->args[1])) {
          fir::Builder b(instr);
          auto i10 = inner1->args[0];
          auto i11 = inner1->args[1];
          auto i20 = inner2->args[0];
          auto i21 = inner2->args[1];
          auto first_op =
              is_base_and ? BinaryInstrSubType::And : BinaryInstrSubType::Or;
          auto fin_op =
              is_base_and ? BinaryInstrSubType::Or : BinaryInstrSubType::And;
          auto a1 = b.build_binary_op(i10, i20, first_op);
          auto a2 = b.build_binary_op(i11, i21, first_op);
          auto res = b.build_binary_op(a1, a2, fin_op);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
          instr.destroy();
          return true;
        }
      }
    }

    const auto *c_val = (c1_val != nullptr) ? c1_val : c0_val;
    const u32 c_idx = (c0_val != nullptr) ? 0 : 1;
    const u32 v_idx = (c1_val != nullptr) ? 0 : 1;
    // if (c_val->is_int() && c_val->get_type()->is_int() &&
    //     instr.get_type()->is_int() &&
    //     c_val->get_type()->get_bitwidth() !=
    //     instr.get_type()->get_bitwidth()) {
    //   instr->args[c_idx] = fir::ValueR{
    //       ctx->get_constant_value(c_val->as_int(), instr.get_type())};
    //   worklist.push_back({instr, instr->get_parent()});
    //   return true;
    // }

    // (x+c1) +y => (x+y)+c1
    if (instr->subtype == (u32)BinaryInstrSubType::IntAdd &&
        instr->args[v_idx].is_instr()) {
      auto sub = instr->args[v_idx].as_instr();
      if (sub->is(BinaryInstrSubType::IntAdd) &&
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
        return true;
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
        return true;
      }
    }

    if ((instr->is(BinaryInstrSubType::Shl) ||
         instr->is(BinaryInstrSubType::Shr) ||
         instr->is(BinaryInstrSubType::AShr)) &&
        instr->args[0].is_instr()) {
      auto argi = instr->args[0].as_instr();
      if (instr->is(BinaryInstrSubType::Shr) &&
          argi->is(fir::BinaryInstrSubType::Shl) &&
          instr->args[1].is_constant() && argi->args[1].is_constant()) {
        auto shiftl_amount = argi->args[1].as_constant()->as_int();
        auto shiftr_amount = instr->args[1].as_constant()->as_int();
        fir::Builder buh{argi};
        fir::ValueR res;
        if (shiftl_amount == shiftr_amount) {
          // (x & ((1 << n) - 1))
          res = buh.build_binary_op(
              argi->args[0],
              fir::ValueR{ctx->get_constant_value(
                  ((i128)1 << shiftr_amount) - 1, instr->get_type())},
              BinaryInstrSubType::And);
        } else if (shiftl_amount > shiftr_amount) {
          // ((1 << (32-20)) -1) << 18
          auto new_shift = buh.build_binary_op(
              argi->args[0],
              fir::ValueR{ctx->get_constant_value(shiftl_amount - shiftr_amount,
                                                  instr->get_type())},
              BinaryInstrSubType::Shl);
          res = buh.build_binary_op(
              new_shift,
              fir::ValueR{ctx->get_constant_value(
                  (((i128)1
                    << (instr->get_type()->get_bitwidth() - shiftl_amount)) -
                   1) << (shiftl_amount - shiftr_amount),
                  instr->get_type())},
              BinaryInstrSubType::And);
        } else {
          auto net_rshift = shiftr_amount - shiftl_amount;
          auto bitwidth = instr->get_type()->get_bitwidth();

          auto rshift_instr =
              buh.build_binary_op(argi->args[0],
                                  fir::ValueR{ctx->get_constant_value(
                                      net_rshift, instr->get_type())},
                                  BinaryInstrSubType::Shr);
          i128 mask_val =
              (((i128)1 << (bitwidth - shiftl_amount)) - 1) >> net_rshift;
          res = buh.build_binary_op(
              rshift_instr,
              fir::ValueR{ctx->get_constant_value(mask_val, instr->get_type())},
              BinaryInstrSubType::And);
        }
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
        instr.destroy();
        return true;
      }
      if (instr->is(BinaryInstrSubType::Shl) &&
          (argi->is(BinaryInstrSubType::Shr) ||
           argi->is(BinaryInstrSubType::AShr))) {
        if (instr->args[1].is_constant() && instr->args[1] == argi->args[1]) {
          fir::Builder buh{argi};
          // (x & ((1 << n) - 1))
          auto shift_amount = instr->args[1].as_constant()->as_int();
          auto mask = ((i128)1 << shift_amount) - 1;
          mask = mask << (instr->get_type()->get_bitwidth() - shift_amount);
          auto res = buh.build_binary_op(
              argi->args[0],
              fir::ValueR{ctx->get_constant_value(mask, instr->get_type())},
              BinaryInstrSubType::And);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
          instr.destroy();
          return true;
        }
        if (instr->args[1].is_constant() && argi->args[1].is_constant() &&
            argi->is(BinaryInstrSubType::AShr)) {
          TODO("impl");
          return true;
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
        return true;
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
        return true;
      }
      if (c_val->as_f64() == 1) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return true;
      }
      if (c_val->as_f64() == 2) {
        fir::Builder buh{instr};
        auto res = buh.build_binary_op(instr->args[0], instr->args[0],
                                       BinaryInstrSubType::FloatAdd);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(res);
        instr.destroy();
        return true;
      }
    }
    if (c_val->is_float() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::FloatAdd) {
      Builder bb{instr};
      if (c_val->as_f64() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return true;
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
        return true;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::Xor) {
      auto val = c_val->as_int();
      if (val == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return true;
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
        return true;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::Or) {
      if (c_val->as_int() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[1 - c_idx]);
        instr.destroy();
        return true;
      }
      auto max_unsigend_val =
          (((u128)1 << c_val->get_type()->get_bitwidth()) - 1);
      if (std::bit_cast<u128>(c_val->as_int()) == max_unsigend_val) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(
            std::bit_cast<i128>(max_unsigend_val), instr->get_type())});
        instr.destroy();
        return true;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::And) {
      auto other_val = instr->args[v_idx];
      if (other_val.is_instr()) {
        auto other_i = other_val.as_instr();
        auto input_mask =
            (((u128)1 << other_i->args[0].get_type()->get_bitwidth()) - 1);
        if (other_i->is(InstrType::ZExt) &&
            (std::bit_cast<u128>(c_val->as_int()) & input_mask) == input_mask) {
          push_all_uses(worklist, instr);
          instr->replace_all_uses(fir::ValueR{other_i});
          instr.destroy();
          return true;
        }
      }
      {
        auto input_mask = (((u128)1 << instr->get_type()->get_bitwidth()) - 1);
        if ((std::bit_cast<u128>(c_val->as_int()) & input_mask) == input_mask) {
          push_all_uses(worklist, instr);
          instr->replace_all_uses(instr->args[1 - c_idx]);
          instr.destroy();
          return true;
        }
      }
      if (c_val->as_int() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value(0, instr.get_type())});
        instr.destroy();
        return true;
      }
    }
    if (c_idx == 1 && c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntSub) {
      if (c_val->as_int() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return true;
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntMul) {
      auto c_vali = c_val->as_int();
      if (c_vali == 1) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return true;
      }
      if (c_vali == 0) {
        auto zero_const = ctx.data->get_constant_value(0, c_val->get_type());
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR{zero_const});
        instr.destroy();
        return true;
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
          return true;
        }
      }
    }
    if (c_val->is_int() &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntAdd) {
      if (c_val->as_int() == 0) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.destroy();
        return true;
      }
      if (instr->args[0].is_instr() && c1_val->get_type()->is_int()) {
        ASSERT(c0_val == nullptr);
        auto a0 = instr->args[0].as_instr();
        if (a0->is(InstrType::BinaryInstr) && a0->args[1].is_constant() &&
            a0->args[1].as_constant()->is_int()) {
          auto sec_constant = a0->args[1].as_constant()->as_int();
          // fmt::println("{}", instr);
          // FIXME: fix potential issue with overflow
          auto biggest_bitwidth =
              std::max(a0->args[1].get_type()->get_bitwidth(),
                       c1_val->get_type()->get_bitwidth());

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
              return true;
            }
            case fir::BinaryInstrSubType::IntSub: {
              auto new_val = ctx->get_constant_int(
                  c1_val->as_int() - sec_constant, biggest_bitwidth);
              instr.replace_arg(0, a0->args[0]);
              instr.replace_arg(1, ValueR(new_val));
              push_all_uses(worklist, instr);
              return true;
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

  auto res_ty = instr->get_type();
  if (res_ty->get_bitwidth() <= 64 &&
      (instr->get_instr_subtype() == (u32)BinaryInstrSubType::And ||
       instr->get_instr_subtype() == (u32)BinaryInstrSubType::Or)) {
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
      return true;
    }
    if (is_redundant1) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[1]);
      instr.destroy();
      return true;
    }
  }

  if (simplify_reduction(instr, bb, ctx, worklist, man)) {
    return true;
  }
  return false;
}

}  // namespace foptim::optim::InstSimp
