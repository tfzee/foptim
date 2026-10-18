#include "simplify_icmp.hpp"

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/attributer/KnownBits.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim::InstSimp {

namespace {
void swap_args_icmp(fir::Instr instr) {
  ASSERT(instr->is(fir::InstrType::ICmp))
  switch (static_cast<fir::ICmpInstrSubType>(instr->subtype)) {
    case fir::ICmpInstrSubType::NE:
    case fir::ICmpInstrSubType::EQ:
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::SLT:
      instr->subtype = static_cast<u32>(fir::ICmpInstrSubType::SGT);
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::ULT:
      instr->subtype = static_cast<u32>(fir::ICmpInstrSubType::UGT);
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::SGT:
      instr->subtype = static_cast<u32>(fir::ICmpInstrSubType::SLT);
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::UGT:
      instr->subtype = static_cast<u32>(fir::ICmpInstrSubType::ULT);
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::UGE:
      instr->subtype = static_cast<u32>(fir::ICmpInstrSubType::ULE);
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::ULE:
      instr->subtype = static_cast<u32>(fir::ICmpInstrSubType::UGE);
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::SGE:
      instr->subtype = static_cast<u32>(fir::ICmpInstrSubType::SLE);
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::SLE:
      instr->subtype = static_cast<u32>(fir::ICmpInstrSubType::SGE);
      swap_args(instr, 0, 1);
      return;
    case fir::ICmpInstrSubType::INVALID:
      UNREACH();
  }
}
}  // namespace

namespace {
// (x & (2^n - 1)) compared with y can be done on the low n bits only if y has
// no bits above them (otherwise the compare sees different values) and the
// predicate does not care about the sign.
static bool icmp_fits_narrow_compare(fir::Instr icmp, u32 n_bits) {
  switch (static_cast<fir::ICmpInstrSubType>(icmp->subtype)) {
    case fir::ICmpInstrSubType::EQ:
    case fir::ICmpInstrSubType::NE:
    case fir::ICmpInstrSubType::ULT:
    case fir::ICmpInstrSubType::ULE:
    case fir::ICmpInstrSubType::UGT:
    case fir::ICmpInstrSubType::UGE:
      break;
    default:
      return false;
  }
  auto other = icmp->args[1];
  if (other.is_constant_int()) {
    auto c = other.as_constant()->as_apint();
    return c.active_bits() <= n_bits;
  }
  if (!other.is_instr()) {
    return false;
  }
  auto oi = other.as_instr();
  if (oi->is(fir::InstrType::ZExt)) {
    return oi->args[0].get_type()->get_bitwidth() <= n_bits;
  }
  if (oi->is(fir::BinaryInstrSubType::And) && oi->args[1].is_constant_int()) {
    return oi->args[1].as_constant()->as_apint().active_bits() <= n_bits;
  }
  return false;
}

bool simplify_icmp_impl(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                        WorkList &worklist, AttributerManager &man);
}  // namespace

bool simplify_icmp(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                   WorkList &worklist, AttributerManager &man) {
  bool normalized = false;
  if ((instr->args[0].is_constant() &&
       (!instr->args[0].as_constant()->is_global() &&
        !instr->args[0].as_constant()->is_func())) &&
      (!instr->args[1].is_constant() ||
       (instr->args[1].as_constant()->is_global() ||
        instr->args[1].as_constant()->is_func()))) {
    const auto before = instr->args[0];
    swap_args_icmp(instr);
    normalized = instr->args[0] != before;
  }
  return simplify_icmp_impl(instr, bb, ctx, worklist, man) || normalized;
}

namespace {
bool simplify_icmp_impl(fir::Instr instr, fir::BasicBlock /*bb*/,
                        fir::Context &ctx, WorkList &worklist,
                        AttributerManager & /*man*/) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyICMP");
  }
  using foptim::fir::BinaryInstrSubType;
  using foptim::fir::Builder;
  using foptim::fir::ICmpInstrSubType;
  using foptim::fir::InstrType;
  using foptim::fir::IntrinsicSubType;
  using foptim::fir::UnaryInstrSubType;
  using foptim::fir::ValueR;
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

  bool second_constant = instr->args[1].is_constant();

  if (second_constant && instr->args[1].as_constant()->is_int()) {
    auto sub_type = static_cast<ICmpInstrSubType>(instr->get_instr_subtype());
    i128 c_val = instr->args[1].as_constant()->as_int_sat();

    if (instr->args[1].get_type()->get_bitwidth() >
        instr->args[0].get_type()->get_bitwidth()) {
      instr->args[1] = fir::ValueR{ctx->get_constant_int(
          instr->args[1].as_constant()->as_apint_resized(
              instr->args[0].get_type()->get_bitwidth()))};
      worklist.push_back({instr, instr->get_parent()});
      return true;
    }

    if (instr->args[0].is_instr()) {
      auto arg0 = instr->args[0].as_instr();
      if (arg0->is(fir::BinaryInstrSubType::And) &&
          arg0->args[1].is_const_int(1)) {
        // mask everything but lowest bit then when do eq/me on 0/1 then we can
        // also just truncate
        if ((c_val == 1 && sub_type == ICmpInstrSubType::EQ) ||
            (c_val == 0 && sub_type == ICmpInstrSubType::NE)) {
          Builder bb{instr};
          auto new_val = bb.build_itrunc(arg0->args[0], ctx->get_int_type(1));
          push_all_uses(worklist, instr);
          instr->replace_all_uses(ValueR(new_val));
          instr.destroy();
          return true;
        }
        // need to invert it here
        if ((c_val == 0 && sub_type == ICmpInstrSubType::EQ) ||
            (c_val == 1 && sub_type == ICmpInstrSubType::NE)) {
          Builder bb{instr};
          auto new_val = bb.build_itrunc(arg0->args[0], ctx->get_int_type(1));
          auto res = bb.build_unary_op(new_val, UnaryInstrSubType::Not);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(ValueR(res));
          instr.destroy();
          return true;
        }
      }
      // NOTE: a / 2^x > 0 -> a > x (if x is between 0-bitwidth)
      if (c_val == 0 && (sub_type == ICmpInstrSubType::SGT) &&
          arg0->is(InstrType::BinaryInstr) &&
          arg0->subtype == static_cast<u32>(BinaryInstrSubType::IntSDiv) &&
          arg0->args[1].is_constant() &&
          // TODO: could also be poision but that gets propagated anyway
          arg0->args[1].as_constant()->is_int()) {
        // auto a = instr->args[1].as_constant()->as_int();
        auto x = arg0->args[1].as_constant()->as_int_sat();
        if (x > 0 && (x & (x - 1)) == 0 && x < arg0->result_type()->as_int()) {
          Builder bb{instr};
          auto new_val = bb.build_int_cmp(arg0->args[0], arg0->args[1],
                                          ICmpInstrSubType::SGE);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(ValueR(new_val));
          instr.destroy();
          return true;
        }
      }

      // sdiv truncates towards zero: with d > 0
      //   x/d <= k  <=>  x <= k*d + (k >= 0 ? d-1 : 0)
      //   x/d >= k  <=>  x >= k*d - (k <= 0 ? d-1 : 0)
      // the bound has to fit the type, otherwise leave it alone
      if ((sub_type == ICmpInstrSubType::SLE ||
           sub_type == ICmpInstrSubType::SLT ||
           sub_type == ICmpInstrSubType::SGE ||
           sub_type == ICmpInstrSubType::SGT) &&
          arg0->is(InstrType::BinaryInstr) &&
          arg0->subtype == static_cast<u32>(BinaryInstrSubType::IntSDiv) &&
          arg0->args[1].is_constant() &&
          arg0->args[1].as_constant()->is_int() &&
          arg0->args[0].get_type()->is_int() &&
          arg0->args[0].get_type()->as_int() <= 64) {
        auto x = arg0->args[0];
        const i128 d = arg0->args[1].as_constant()->as_int();
        const u32 bw = x.get_type()->as_int();
        const i128 lo = -(static_cast<i128>(1) << (bw - 1));
        const i128 hi = (static_cast<i128>(1) << (bw - 1)) - 1;
        const bool is_le = sub_type == ICmpInstrSubType::SLE ||
                           sub_type == ICmpInstrSubType::SLT;
        if (d > 0 && c_val >= lo && c_val <= hi) {
          i128 bound = 0;
          if (is_le) {
            const i128 k =
                sub_type == ICmpInstrSubType::SLE ? c_val : c_val - 1;
            bound = k * d + (k >= 0 ? d - 1 : 0);
          } else {
            const i128 k =
                sub_type == ICmpInstrSubType::SGE ? c_val : c_val + 1;
            bound = k * d - (k <= 0 ? d - 1 : 0);
          }
          if (bound >= lo && bound <= hi) {
            Builder bb{instr};
            auto new_val = bb.build_int_cmp(
                x, fir::ValueR{ctx->get_constant_value(bound, x.get_type())},
                is_le ? ICmpInstrSubType::SLE : ICmpInstrSubType::SGE);
            push_all_uses(worklist, instr);
            instr->replace_all_uses(ValueR(new_val));
            instr.destroy();
            return true;
          }
        }
      }
    }

    if ((sub_type == ICmpInstrSubType::SLT) && instr->args[0].is_instr()) {
      auto arg0 = instr->args[0].as_instr();
      if (arg0->is(InstrType::Intrinsic) &&
          arg0->subtype == static_cast<u32>(IntrinsicSubType::Abs) &&
          c_val == 1) {
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

    if ((sub_type == ICmpInstrSubType::EQ ||
         sub_type == ICmpInstrSubType::NE) &&
        instr->args[0].is_instr()) {
      auto arg0 = instr->args[0].as_instr();
      if (arg0->is(InstrType::BinaryInstr) &&
          arg0->subtype == static_cast<u32>(BinaryInstrSubType::IntSub) &&
          c_val == 0) {
        fir::Builder b{instr};
        auto new_cmp = b.build_int_cmp(arg0->args[0], arg0->args[1], sub_type);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_cmp);
        instr.destroy();
        return true;
      }
      auto arg0Ty = instr->args[0].get_type();
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
            1, fir::ValueR{ctx->get_constant_int(
                   instr->args[1].as_constant()->as_apint_resized(
                       x.get_type()->get_bitwidth()))});
        worklist.emplace_back(instr, instr->get_parent());
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
        if (a1i->instr_type == InstrType::ZExt) {
          // zero extended values are never negative in the wide type but the
          // narrow ones can be, so a signed compare has to become unsigned
          switch (static_cast<ICmpInstrSubType>(instr->subtype)) {
            case ICmpInstrSubType::SLT:
              instr->subtype = static_cast<u32>(ICmpInstrSubType::ULT);
              break;
            case ICmpInstrSubType::SLE:
              instr->subtype = static_cast<u32>(ICmpInstrSubType::ULE);
              break;
            case ICmpInstrSubType::SGT:
              instr->subtype = static_cast<u32>(ICmpInstrSubType::UGT);
              break;
            case ICmpInstrSubType::SGE:
              instr->subtype = static_cast<u32>(ICmpInstrSubType::UGE);
              break;
            default:
              break;
          }
        }
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
        (instr->get_instr_subtype() ==
             static_cast<u32>(ICmpInstrSubType::UGE) ||
         instr->get_instr_subtype() ==
             static_cast<u32>(ICmpInstrSubType::ULT))) {
      fir::Builder buh{instr};
      auto a = a1i->args[1] == instr->args[1] ? a1i->args[0] : a1i->args[1];
      auto b = instr->args[1];
      auto xord = buh.build_binary_op(
          b,
          fir::ValueR{ctx->get_constant_value(static_cast<i128>(-1L),
                                              instr->args[1].get_type())},
          BinaryInstrSubType::Xor);
      auto res = buh.build_int_cmp(
          a, xord,
          instr->get_instr_subtype() == static_cast<u32>(ICmpInstrSubType::UGE)
              ? ICmpInstrSubType::ULE
              : ICmpInstrSubType::UGT);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return true;
    }
    if (a1i->is(InstrType::BinaryInstr) &&
        a1i->subtype == static_cast<u32>(BinaryInstrSubType::And) &&
        a1i->args[1].is_constant()) {
      auto const_and = a1i->args[1].as_constant()->as_int_sat();
      // could also check <= and insert a itrunc previously
      for (auto amount : {8, 16, 32}) {
        if (const_and == (1UL << amount) - 1 &&
            icmp_fits_narrow_compare(instr, static_cast<u32>(amount))) {
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
}  // namespace

}  // namespace foptim::optim::InstSimp
