#include "inst_simplify.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "optim/analysis/attributer/KnownStackBits.hpp"
#include "optim/analysis/attributer/attributer.hpp"

namespace foptim::optim {

using WorkList = TVec<InstSimplify::WorkItem>;

static void swap_args(fir::Instr instr, u32 a1, u32 a2) {
  using namespace foptim::fir;
  auto v1 = instr->args[a1];
  auto v2 = instr->args[a2];
  instr.replace_arg(a1, v2);
  instr.replace_arg(a2, v1);
}
static void swap_args_fcmp(fir::Instr instr) {
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
static void swap_args_icmp(fir::Instr instr) {
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
static void push_all_uses(WorkList &worklist, fir::Instr instr) {
  for (auto &use : instr->uses) {
    worklist.emplace_back(use.user, use.user->parent);
  }
}

static void simplify_binary(fir::Instr instr, fir::BasicBlock bb,
                            fir::Context &ctx, WorkList &worklist,
                            AttributerManager &man) {
  using namespace foptim::fir;
  // since both being constant would be handleded by constant folding we just
  // asume theres one and normalzie by putting it into the secodn arg
  {
    if (instr->is_commutative() && instr->args[0].is_constant() &&
        !instr->args[0].as_constant()->is_global() &&
        !instr->args[0].as_constant()->is_func()) {
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
  {
    if ((instr->subtype == (u32)BinaryInstrSubType::IntAdd ||
         instr->subtype == (u32)BinaryInstrSubType::IntSub) &&
        instr->args[1].is_constant() && instr->args[0].is_instr()) {
      auto inner_add_sub = instr->args[0].as_instr();
      if (inner_add_sub->is(InstrType::BinaryInstr) &&
          (inner_add_sub->subtype == (u32)BinaryInstrSubType::IntAdd ||
           inner_add_sub->subtype == (u32)BinaryInstrSubType::IntSub) &&
          inner_add_sub->args[1].is_constant()) {
        fir::Builder b(instr);
        auto const2 = instr->args[1];
        auto x = inner_add_sub->args[0];
        auto const1 = inner_add_sub->args[1];
        if (inner_add_sub->subtype == (u32)BinaryInstrSubType::IntAdd &&
            instr->subtype == (u32)BinaryInstrSubType::IntAdd) {
          auto a1 = b.build_int_add(const1, const2);
          auto res = b.build_int_add(x, a1);
          worklist.emplace_back(a1.as_instr(), bb);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
        } else if ((inner_add_sub->subtype == (u32)BinaryInstrSubType::IntSub &&
                    instr->subtype == (u32)BinaryInstrSubType::IntSub)) {
          auto a1 = b.build_int_add(const1, const2);
          auto res = b.build_int_sub(x, a1);
          worklist.emplace_back(a1.as_instr(), bb);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
        } else if ((inner_add_sub->subtype == (u32)BinaryInstrSubType::IntAdd &&
                    instr->subtype == (u32)BinaryInstrSubType::IntSub)) {
          auto a1 = b.build_int_sub(const1, const2);
          auto res = b.build_int_add(x, a1);
          worklist.emplace_back(a1.as_instr(), bb);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);
        } else if ((inner_add_sub->subtype == (u32)BinaryInstrSubType::IntSub &&
                    instr->subtype == (u32)BinaryInstrSubType::IntAdd)) {
          auto a1 = b.build_int_sub(const2, const1);
          auto res = b.build_int_add(x, a1);
          worklist.emplace_back(a1.as_instr(), bb);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(res);

        } else {
          UNREACH();
        }
        instr.destroy();
        return;
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
  if (!has_const) {
    return;
  }

  if ((c0_val && c0_val->is_poison()) || (c1_val && c1_val->is_poison())) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(ValueR{ctx->get_poisson_value(instr.get_type())});
    instr.destroy();
    return;
  }
  if ((c0_val && c0_val->is_null()) || (c1_val && c1_val->is_null())) {
    auto index = (c0_val && c0_val->is_null()) ? 1 : 0;
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

  // at this point it cant have both as constant
  // if (c_val->is_float()) {
  // TODO: need to check also if its special constant with sign bit and shit
  //  if (c_val->as_float() == 0 &&
  //      instr->get_instr_subtype() == (u32)BinaryInstrSubType::FloatAdd) {
  //    push_all_uses(worklist, instr);
  //    instr->replace_all_uses(instr->args[v_idx]);
  //    instr.destroy();
  //    return;
  //  }
  //  if (instr->get_instr_subtype() == (u32)BinaryInstrSubType::FloatMul) {
  //    if (c_val->as_float() == 1) {
  //      push_all_uses(worklist, instr);
  //      instr->replace_all_uses(instr->args[v_idx]);
  //      instr.destroy();
  //    } else if (c_val->as_float() == 0) {
  //      auto zero_const = ctx.data->get_constant_value(.0,
  //      c_val->get_type()); push_all_uses(worklist, instr);
  //      instr->replace_all_uses(ValueR{zero_const});
  //      instr.destroy();
  //    }
  //    return;
  //  }
  // }

  if (c_val->is_int() &&
      instr->get_instr_subtype() == (u32)BinaryInstrSubType::Xor) {
    auto val = c_val->as_int();
    auto bit_width = instr->get_type()->as_int();
    u64 all_one_mask = ~0;
    if (bit_width < 64) {
      all_one_mask = (1UL << bit_width) - 1;
    }
    if (val == all_one_mask) {
      instr->args.erase(instr->args.begin() + c_idx);
      instr->instr_type = InstrType::UnaryInstr;
      instr->subtype = (u32)UnaryInstrSubType::Not;
      push_all_uses(worklist, instr);
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
          auto new_val =
              ctx->get_constant_value(c1_val->as_int() + sec_constant,
                                      ctx->get_int_type(biggest_bitwidth));
          instr.replace_arg(0, a0->args[0]);
          instr.replace_arg(1, ValueR(new_val));
          push_all_uses(worklist, instr);
          break;
        }
        case fir::BinaryInstrSubType::IntSub: {
          auto new_val =
              ctx->get_constant_value(c1_val->as_int() - sec_constant,
                                      ctx->get_int_type(biggest_bitwidth));
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
  if (c_val->is_int() &&
      instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntMul) {
    if (c_val->as_int() == 1) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[v_idx]);
      instr.destroy();
      return;
    }
    if (c_val->as_int() == 0) {
      auto zero_const = ctx.data->get_constant_value(0, c_val->get_type());
      push_all_uses(worklist, instr);
      instr->replace_all_uses(ValueR{zero_const});
      instr.destroy();
      return;
    }
  }
  // handle 0*constant
  if (c1_val && c1_val->is_int() &&
      instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntMul) {
    if (c1_val->as_int() == 1) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[0]);
      instr.destroy();
      return;
    }
    if (c1_val->as_int() == 0) {
      auto zero_const = ctx.data->get_constant_value(0, c1_val->get_type());
      push_all_uses(worklist, instr);
      instr->replace_all_uses(ValueR{zero_const});
      instr.destroy();
      return;
    }
  }

  // bit patterns
  // if (instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntAdd &&
  //     instr->args[1].is_constant() && instr->args[1].as_constant()->is_int()) {
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
    man.run();

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
}

static void simplify_icmp(fir::Instr instr, fir::BasicBlock /*bb*/,
                          fir::Context &ctx, WorkList &worklist,
                          AttributerManager &man) {
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
          auto new_const_value =
              ctx->get_constant_value((u32)is_true, ctx->get_int_type(8));
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
      auto bitwidth = std::max(c1->type->as_int(), c2->type->as_int());
      i128 mask = ~(((i128)1 << bitwidth) - 1);
      is_true = (output & mask) != 0;
    } break;

    case fir::ICmpInstrSubType::AddOverflow: {
      i128 output = v1 + v2;
      auto bitwidth = std::max(c1->type->as_int(), c2->type->as_int());
      i128 mask = ~(((i128)1 << bitwidth) - 1);
      is_true = (output & mask) != 0;
    } break;
    }
    auto new_const_value =
        ctx->get_constant_value((u64)is_true, ctx->get_int_type(8));
    push_all_uses(worklist, instr);
    instr->replace_all_uses(ValueR(new_const_value));
    instr.destroy();
    return;
  }

  if (second_constant && instr->args[1].as_constant()->is_int()) {
    auto sub_type = (ICmpInstrSubType)instr->get_instr_subtype();
    i128 c_val = instr->args[1].as_constant()->as_int();

    // NOTE: a / 2^x > 0 -> a > x (if x is between 0-bitwidth)
    if (c_val == 0 && (sub_type == ICmpInstrSubType::SGT) &&
        instr->args[0].is_instr()) {
      auto arg0 = instr->args[0].as_instr();
      if (arg0->is(InstrType::BinaryInstr) &&
          arg0->subtype == (u32)BinaryInstrSubType::IntSDiv &&
          arg0->args[1].is_constant() &&
          // TODO: could also be poision
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
      man.run();
      auto msb_res = bits->msb_info();
      bool value_known_negative = msb_res == KnownBits::KnownOne;
      bool value_known_positive = msb_res == KnownBits::KnownZero;
      bool evals_to_true = (value_known_negative && check_negative) ||
                           (value_known_positive && check_positive);
      bool evals_to_false = (value_known_negative && check_positive) ||
                            (value_known_positive && check_negative);
      if (evals_to_false || evals_to_true) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR(ctx->get_constant_value(
            evals_to_true ? 1 : 0, ctx->get_int_type(1))));
        instr.destroy();
        return;
      }
    }
  }
  auto sub_type = (ICmpInstrSubType)instr->get_instr_subtype();
  if (sub_type == ICmpInstrSubType::UGT || sub_type == ICmpInstrSubType::UGE ||
      sub_type == ICmpInstrSubType::ULT || sub_type == ICmpInstrSubType::ULE) {
    const auto *bits1 = man.get_or_create_analysis<KnownBits>(instr->args[0]);
    const auto *bits2 = man.get_or_create_analysis<KnownBits>(instr->args[1]);
    man.run();
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
      instr->replace_all_uses(ValueR(ctx->get_constant_value(
          evals_to_true ? 1 : 0, ctx->get_int_type(1))));
      instr.destroy();
      return;
    }
  }

  if (sub_type == ICmpInstrSubType::SGT || sub_type == ICmpInstrSubType::SGE ||
      sub_type == ICmpInstrSubType::SLT || sub_type == ICmpInstrSubType::SLE) {
    const auto *bits1 = man.get_or_create_analysis<KnownBits>(instr->args[0]);
    const auto *bits2 = man.get_or_create_analysis<KnownBits>(instr->args[1]);
    man.run();
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
      instr->replace_all_uses(ValueR(ctx->get_constant_value(
          evals_to_true ? 1 : 0, ctx->get_int_type(1))));
      instr.destroy();
      return;
    }
  }

  if (sub_type == ICmpInstrSubType::EQ || sub_type == ICmpInstrSubType::NE) {
    const auto *bits1 = man.get_or_create_analysis<KnownBits>(instr->args[0]);
    const auto *bits2 = man.get_or_create_analysis<KnownBits>(instr->args[1]);
    man.run();
    bool evals_to_true = false;
    bool evals_to_false = false;
    if (sub_type == ICmpInstrSubType::EQ) {
      evals_to_false = (bits1->known_one & bits2->known_zero) != 0 ||
                       (bits1->known_zero & bits2->known_one) != 0;
    } else if (sub_type == ICmpInstrSubType::NE) {
      evals_to_true = (bits1->known_one & bits2->known_zero) != 0 ||
                      (bits1->known_zero & bits2->known_one) != 0;
    }
    (void)evals_to_false;
    (void)evals_to_true;

    // ASSERT(!evals_to_false || !evals_to_true);
    // if (evals_to_true || evals_to_false) {
    //   push_all_uses(worklist, instr);
    //   instr->replace_all_uses(ValueR(ctx->get_constant_value(
    //       evals_to_true ? 1 : 0, ctx->get_int_type(1))));
    //   instr.destroy();
    //   return;
    // }
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

static void simplify_fcmp(fir::Instr instr, fir::BasicBlock /*bb*/,
                          fir::Context &ctx, WorkList &worklist) {
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

  if (first_constant && second_constant) {
    const auto c1 = instr->args[0].as_constant();
    const auto c2 = instr->args[1].as_constant();
    ASSERT(c1->is_float());
    const auto v1 = c1->as_float();
    const auto v2 = c2->as_float();

    bool is_true = false;
    // IMPORTANT: !!THIS IS IN OTHER SYNTAX SO FLIPPED ARGUMETNS!!
    // IMPORTANT: !!THIS IS IN OTHER SYNTAX SO FLIPPED ARGUMETNS!!
    switch ((FCmpInstrSubType)instr->get_instr_subtype()) {
    case fir::FCmpInstrSubType::IsNaN:
      __asm__("vcomisd %2, %1\n\t"
              "setp %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OEQ:
      __asm__("vcomisd %2, %1\n\t"
              "sete %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OGT:
      __asm__("vcomisd %2, %1\n\t"
              "seta %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OGE:
      __asm__("vcomisd %2, %1\n\t"
              "setae %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OLT:
      __asm__("vcomisd %2, %1\n\t"
              "setb %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OLE:
      __asm__("vcomisd %2, %1\n\t"
              "setbe %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::ONE:
      __asm__("vcomisd %2, %1\n\t"
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
      __asm__("vucomisd %2, %1\n\t"
              "setne %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::INVALID:
    case fir::FCmpInstrSubType::AlwFalse:
    case fir::FCmpInstrSubType::AlwTrue:
      return;
    }
    auto new_const_value =
        ctx->get_constant_value((u64)is_true, ctx->get_int_type(8));
    push_all_uses(worklist, instr);
    instr->replace_all_uses(ValueR(new_const_value));
    ASSERT(instr->bbs.size() == 0);
    instr.destroy();
    return;
  }
}

static void simplify_select(fir::Instr instr, fir::BasicBlock /*bb*/,
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
}
static void simplify_cond_branch(fir::Instr instr, fir::BasicBlock bb,
                                 fir::Context & /*ctx*/,
                                 WorkList & /*worklist*/) {
  if (instr->args[0].is_constant()) {
    fir::Builder b(bb);
    b.at_end(bb);

    auto v1 = instr->args[0].as_constant()->as_int();
    auto *target = &instr->bbs[0];
    if (v1 == 0) {
      target = &instr->bbs[1];
    }

    auto new_branch = b.build_branch(target->bb);
    for (auto old_arg : target->args) {
      new_branch.add_bb_arg(0, old_arg);
    }
    instr.remove_from_parent();
    return;
  }
}

static void simplify_switch_branch(fir::Instr instr, fir::BasicBlock bb,
                                   fir::Context & /*ctx*/,
                                   WorkList & /*worklist*/) {
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

static void simplify_extend(fir::Instr instr, fir::BasicBlock /*bb*/,
                            fir::Context &ctx, WorkList &worklist) {
  // TODO: could also maybe figure out cases where we can convert everything
  // into higher bitwidth
  (void)ctx;
  if (instr->args[0].is_constant() ||
      instr->args[0].get_type() == instr.get_type()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[0]);
    instr.destroy();
    return;
  }
  if (instr->is(fir::InstrType::ZExt) && instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ITrunc)) {
    auto trunc = instr->args[0].as_instr();
    if (trunc->args[0].get_type() == instr.get_type()) {
      fir::Builder b(instr);
      auto bitwidth = trunc.get_type()->get_size() * 8;
      auto mask =
          ctx->get_constant_value(((u64)1 << bitwidth) - 1, instr->get_type());
      auto new_result = b.build_binary_op(trunc->args[0], fir::ValueR{mask},
                                          fir::BinaryInstrSubType::And);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(new_result);
      instr.destroy();
      return;
    }
  }
}

static void simplify_itrunc(fir::Instr instr, fir::BasicBlock /*bb*/,
                            fir::Context &ctx, WorkList &worklist) {
  (void)instr;
  (void)worklist;
  // ext
  // +-*
  // itrunc
  // = +-* on smaller bitwidth
  // since lea doesnt like 1byte value we skip those for now
  if (instr->args[0].is_instr() && instr.get_type()->get_size() > 1) {
    auto inner_math = instr->args[0].as_instr();
    if (!inner_math->is(fir::InstrType::BinaryInstr)) {
      return;
    }
    bool b0 = inner_math->args[0].is_constant();
    bool b0I = (inner_math->args[0].is_instr() &&
                (inner_math->args[0].as_instr()->is(fir::InstrType::SExt) ||
                 inner_math->args[0].as_instr()->is(fir::InstrType::ZExt)));
    bool b1 = inner_math->args[1].is_constant();
    bool b1I = (inner_math->args[1].is_instr() &&
                (inner_math->args[1].as_instr()->is(fir::InstrType::SExt) ||
                 inner_math->args[1].as_instr()->is(fir::InstrType::ZExt)));
    auto new_type = instr.get_type();

    if ((b0 || b0I) && (b1 || b1I)) {
      fir::Builder b(instr);
      fir::ValueR a0 = inner_math->args[0];
      fir::ValueR a1 = inner_math->args[1];
      if (b0I) {
        a0 = inner_math->args[0].as_instr()->args[0];
      } else {
        auto v = inner_math->args[0].as_constant()->as_int();
        a0 = fir::ValueR{ctx->get_constant_value(v, new_type)};
      }
      if (b1I) {
        a1 = inner_math->args[1].as_instr()->args[0];
      } else {
        auto v = inner_math->args[1].as_constant()->as_int();
        a1 = fir::ValueR{ctx->get_constant_value(v, new_type)};
      }
      // fmt::println("{} {}", inner_math, instr);
      // fmt::println("{} {}", a0.get_type(), a1.get_type());

      auto res = b.build_binary_op(
          a0, a1, (fir::BinaryInstrSubType)inner_math->subtype);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
    }
  }
}

static void simplify_unary(fir::Instr instr, fir::BasicBlock /*bb*/,
                           fir::Context &ctx, WorkList &worklist) {
  if (instr->args[0].is_constant() &&
      instr->args[0].as_constant()->is_poison()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(
        fir::ValueR{ctx->get_poisson_value(instr.get_type())});
    instr.destroy();
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
  if (!instr->args[0].is_constant()) {
    return;
  }
  if ((fir::UnaryInstrSubType)instr->subtype == fir::UnaryInstrSubType::Not) {
    push_all_uses(worklist, instr);
    auto out_type = instr.get_type();
    ASSERT(out_type->is_int());
    auto mask = (1 << out_type->as_int()) - 1;
    push_all_uses(worklist, instr);
    instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(
        (~instr->args[0].as_constant()->as_int()) & mask, out_type)});
    instr.destroy();
    return;
  }
}

static void simplify_conversion(fir::Instr instr, fir::BasicBlock /*bb*/,
                                fir::Context &ctx, WorkList &worklist) {
  (void)ctx;
  (void)worklist;
  switch ((fir::ConversionSubType)instr->subtype) {
  case fir::ConversionSubType::INVALID:
    TODO("unreach");
  case fir::ConversionSubType::FPTOSI:
    // if (instr->args[0].is_constant() &&
    //     instr->args[0].as_constant()->is_float()) {
    //   auto val = instr->args[0].as_constant()->as_float();
    //   push_all_uses(worklist, instr);
    //   instr->replace_all_uses(
    //       fir::ValueR{ctx->get_constant_value((u64)val,
    //       instr->get_type())});
    //   instr.destroy();
    //   TODO("OKAK IMPL");
    //   return;
    // }
  case fir::ConversionSubType::FPTOUI:
    // if (instr->args[0].is_constant() &&
    //     instr->args[0].as_constant()->is_float()) {
    //   auto val = instr->args[0].as_constant()->as_float();
    //   push_all_uses(worklist, instr);
    //   instr->replace_all_uses(
    //       fir::ValueR{ctx->get_constant_value((u64)val,
    //       instr->get_type())});
    //   instr.destroy();
    //   TODO("OKAK IMPL");
    //   return;
    // }
  case fir::ConversionSubType::FPEXT:
  case fir::ConversionSubType::FPTRUNC:
  case fir::ConversionSubType::UITOFP:
  case fir::ConversionSubType::SITOFP:
  case fir::ConversionSubType::PtrToInt:
  case fir::ConversionSubType::IntToPtr:
    return;
  }
}

static void simplify_store(fir::Instr instr, fir::BasicBlock bb,
                           fir::Context &ctx, WorkList &worklist) {
  (void)bb;
  (void)ctx;
  (void)worklist;
  if (instr->args[0].is_constant() &&
      instr->args[0].as_constant()->is_poison()) {
    instr.destroy();
  }
}

static void simplify_load(fir::Instr instr, fir::BasicBlock bb,
                          fir::Context &ctx, WorkList &worklist) {
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
      if (glob->reloc_info.empty()) {
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
}

static void simplify(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                     WorkList &worklist, AttributerManager &man) {
  using namespace foptim::fir;
  auto instr_ty = instr->get_instr_type();
  if (instr_ty == InstrType::BinaryInstr) {
    return simplify_binary(instr, bb, ctx, worklist, man);
  }
  if (instr_ty == InstrType::UnaryInstr) {
    return simplify_unary(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::ICmp) {
    return simplify_icmp(instr, bb, ctx, worklist, man);
  }
  if (instr_ty == InstrType::FCmp) {
    return simplify_fcmp(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::SelectInstr) {
    return simplify_select(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::CondBranchInstr) {
    return simplify_cond_branch(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::SwitchInstr) {
    return simplify_switch_branch(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::SExt || instr_ty == InstrType::ZExt) {
    return simplify_extend(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::ITrunc) {
    return simplify_itrunc(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::Conversion) {
    return simplify_conversion(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::StoreInstr) {
    return simplify_store(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::LoadInstr) {
    return simplify_load(instr, bb, ctx, worklist);
  }
}

void InstSimplify::apply(fir::Context &ctx, fir::Function &func) {
  using namespace foptim::fir;
  AttributerManager man;

  // TODO: maybe replace with actual queue
  TVec<WorkItem> worklist;
  for (BasicBlock bb : func.basic_blocks) {
    auto &instrs = bb->get_instrs();
    for (size_t instr_id = 0; instr_id < instrs.size(); instr_id++) {
      worklist.emplace_back(instrs[instr_id], bb);
    }
  }

  while (!worklist.empty()) {
    auto [instr, bb] = worklist.back();
    worklist.pop_back();
    if (!instr.is_valid() || !instr->parent.is_valid()) {
      continue;
    }
    simplify(instr, bb, ctx, worklist, man);
  }
}

} // namespace foptim::optim
