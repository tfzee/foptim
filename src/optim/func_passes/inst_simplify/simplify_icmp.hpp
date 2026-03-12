

#pragma once
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "optim/analysis/attributer/KnownBits.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim::InstSimp {
namespace {

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
}  // namespace

inline bool simplify_icmp(fir::Instr instr, fir::BasicBlock /*bb*/, fir::Context &ctx,
                   WorkList &worklist, AttributerManager &man) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyICMP");
  }
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
      return true;
    }
    if (c1->is_global() && c2->is_global()) {
      bool are_equal = c1->as_global() == c2->as_global();
      if ((are_equal && instr->is(ICmpInstrSubType::EQ)) ||
          (!are_equal && instr->is(ICmpInstrSubType::NE))) {
        auto new_const_value = ctx->get_constant_int(1, 8);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR(new_const_value));
        ASSERT(instr->bbs.size() == 0);
        instr.destroy();
        return true;
      } else if ((!are_equal && instr->is(ICmpInstrSubType::EQ)) ||
                 (are_equal && instr->is(ICmpInstrSubType::NE))) {
        auto new_const_value = ctx->get_constant_int(0, 8);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR(new_const_value));
        ASSERT(instr->bbs.size() == 0);
        instr.destroy();
        return true;
      }
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
        if (first_known_null && second_known_null) {
          is_true = is_eq;
          is_known = true;
        }

        if (is_known) {
          auto new_const_value = ctx->get_constant_int((u32)is_true, 8);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(ValueR(new_const_value));
          instr.destroy();
          return true;
        }
      }
    }
    if (c1->is_global() || c1->is_func() || c2->is_global() || c2->is_func()) {
      return false;
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
    return true;
  }

  if (second_constant && instr->args[1].as_constant()->is_int()) {
    auto sub_type = (ICmpInstrSubType)instr->get_instr_subtype();
    i128 c_val = instr->args[1].as_constant()->as_int();

    if (instr->args[1].get_type()->get_bitwidth() >
        instr->args[0].get_type()->get_bitwidth()) {
      instr->args[1] = fir::ValueR{
          ctx->get_constant_value(c_val, instr->args[0].get_type())};
      worklist.push_back({instr, instr->get_parent()});
      return true;
    }

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
          return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
      }
      auto arg0Ty = arg0->get_type();
      if (arg0Ty->is_int() && arg0Ty->as_int() == 1 && c_val == 0) {
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
          return true;
        }
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return true;
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
        return true;
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
        return true;
      }
    }
  }

  if (instr->args[0].is_instr()) {
    auto a1i = instr->args[0].as_instr();
    if (instr->args[1].is_instr()) {
      auto a2i = instr->args[1].as_instr();
      if (a1i->instr_type == a2i->instr_type && a1i->subtype == a2i->subtype &&
          (a1i->instr_type == InstrType::ZExt ||
           a1i->instr_type == InstrType::SExt) &&
          a1i->args[0].get_type() == a2i->args[0].get_type()) {
        // TODO: technically this could also work if both are zext but different
        // types cause then we still
        //  could get rid of one of the zext
        instr.replace_arg(0, a1i->args[0]);
        instr.replace_arg(1, a2i->args[0]);
        return true;
      }
    }
    // when we have a+b < b or a+b >= b then we can convert that to h = xor b 1
    // + inverted test of a b
    // TODO: might not be worth it if theres multiple uses of the intadd
    // since we then dont save a instruction but it can potentially paralelize
    // since we dont have a data dependency on the xor
    if (a1i->is(BinaryInstrSubType::IntAdd) &&
        (a1i->args[1] == instr->args[1] || a1i->args[0] == instr->args[1]) &&
        (instr->get_instr_subtype() == (u32)ICmpInstrSubType::UGE ||
         instr->get_instr_subtype() == (u32)ICmpInstrSubType::ULT)) {
      fir::Builder buh{instr};
      auto a = a1i->args[1] == instr->args[1] ? a1i->args[0] : a1i->args[1];
      auto b = instr->args[1];
      auto xord =
          buh.build_binary_op(b,
                              fir::ValueR{ctx->get_constant_value(
                                  (i128)-1L, instr->args[1].get_type())},
                              BinaryInstrSubType::Xor);
      auto res = buh.build_int_cmp(
          a, xord,
          instr->get_instr_subtype() == (u32)ICmpInstrSubType::UGE
              ? ICmpInstrSubType::ULE
              : ICmpInstrSubType::UGT);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return true;
    }
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
          return true;
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
        return true;
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
        return true;
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
        // fmt::println("{:cd}", instr->get_parent());
        // fmt::println("{}\n{}", *bits1, *bits2);
        // fmt::println("{:b}", bits1->known_one & bits2->known_zero);
        // fmt::println("{:b}", bits1->known_zero & bits2->known_one);
        // fmt::println("cc {} {}", evals_to_true, evals_to_false);
        // TODO("okak??");
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            ValueR(ctx->get_constant_int(evals_to_true ? 1 : 0, 1)));
        instr.destroy();
        return true;
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
  return false;
}

}  // namespace foptim::optim::InstSimp
