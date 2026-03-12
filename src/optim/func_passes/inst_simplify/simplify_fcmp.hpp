#pragma once
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim::InstSimp {
namespace {

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

// can drop cast from float to double with some less/greater  if we dont cross
// decision boundaries
bool canNarrowFpCompare(double C, fir::FCmpInstrSubType op) {
  // idk if this would be legal to check
  if (std::isnan(C)) {
    return false;
  }

  if (!std::isfinite(C)) {
    return false;
  }

  auto Cf = static_cast<f32>(C);

  // get da next and previous float so we can check fi its still in teh same
  // boundaries
  f32 prev_f = std::nextafterf(Cf, -std::numeric_limits<f32>::infinity());
  f32 next_f = std::nextafterf(Cf, std::numeric_limits<f32>::infinity());
  auto p = static_cast<f64>(prev_f);
  auto n = static_cast<f64>(next_f);

  switch (op) {
    case fir::FCmpInstrSubType::OGT:
    case fir::FCmpInstrSubType::OLT:
      return p < C && C < n;
    case fir::FCmpInstrSubType::OLE:
      return p < C && C <= static_cast<double>(Cf);
    case fir::FCmpInstrSubType::OGE:
      return static_cast<double>(Cf) <= C && C < n;
    case fir::FCmpInstrSubType::UGT:
    case fir::FCmpInstrSubType::ULT:
      return p < C && C < n;
    case fir::FCmpInstrSubType::ULE:
      return p < C && C <= static_cast<double>(Cf);
    case fir::FCmpInstrSubType::UGE:
      return static_cast<double>(Cf) <= C && C < n;
    case fir::FCmpInstrSubType::OEQ:
    case fir::FCmpInstrSubType::UEQ:
    case fir::FCmpInstrSubType::ONE:
    case fir::FCmpInstrSubType::UNE:
      return static_cast<double>(Cf) == C;
    case fir::FCmpInstrSubType::ORD:
    case fir::FCmpInstrSubType::UNO:
    case fir::FCmpInstrSubType::INVALID:
    case fir::FCmpInstrSubType::AlwFalse:
    case fir::FCmpInstrSubType::AlwTrue:
    case fir::FCmpInstrSubType::IsNaN:
      return false;
  }

  TODO("UNREACH");
}
}  // namespace

inline bool simplify_fcmp(fir::Instr instr, fir::BasicBlock /*bb*/, fir::Context &ctx,
                   WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyFCMP");
  }
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
  auto fcmp_subtype = (FCmpInstrSubType)instr->get_instr_subtype();

  // conert to integer comparison
  if (instr->args[0].is_instr() && second_constant) {
    auto arg0_i = instr->args[0].as_instr();
    auto const1_i = instr->args[1].as_constant();
    if (const1_i->is_float() && arg0_i->is(ConversionSubType::FPEXT)) {
      auto const_val = const1_i->as_f64();
      if (arg0_i->args[0].get_type()->as_float() == 32 &&
          arg0_i.get_type()->as_float() == 64 &&
          canNarrowFpCompare(const_val, fcmp_subtype)) {
        fir::Builder buh{instr};
        auto new_cmp =
            buh.build_float_cmp(arg0_i->args[0],
                                fir::ValueR{ctx->get_constant_value(
                                    (f32)const_val, ctx->get_float_type(32))},
                                fcmp_subtype);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_cmp);
        instr.destroy();
        if (arg0_i->get_n_uses() == 1) {
          arg0_i.destroy();
        }
        return true;
      }
    } else if (const1_i->is_float() && arg0_i->is(ConversionSubType::SITOFP)) {
      fir::Builder b{instr};
      // TODO: this is iffy
      // will prob fail for very big constants and or very big values that both
      // have problems in getting represented in the other
      auto const_val = const1_i->as_f64();

      switch (fcmp_subtype) {
        case fir::FCmpInstrSubType::OLT: {
          auto r = b.build_int_cmp(
              arg0_i->args[0],
              fir::ValueR{ctx->get_constant_value((i128)ceil(const_val),
                                                  arg0_i->args[0].get_type())},
              ICmpInstrSubType::SLT);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(r);
          instr.destroy();
          return true;
        }
        case fir::FCmpInstrSubType::OGT: {
          auto r = b.build_int_cmp(
              arg0_i->args[0],
              fir::ValueR{ctx->get_constant_value((i128)floor(const_val),
                                                  arg0_i->args[0].get_type())},
              ICmpInstrSubType::SGT);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(r);
          instr.destroy();
          return true;
        }
        case fir::FCmpInstrSubType::UNE: {
          auto r = b.build_int_cmp(
              arg0_i->args[0],
              fir::ValueR{ctx->get_constant_value((i128)floor(const_val),
                                                  arg0_i->args[0].get_type())},
              ICmpInstrSubType::NE);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(r);
          instr.destroy();
          return true;
        }
        case fir::FCmpInstrSubType::INVALID:
        case fir::FCmpInstrSubType::AlwFalse:
        case fir::FCmpInstrSubType::OEQ:
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
        case fir::FCmpInstrSubType::AlwTrue:
        case fir::FCmpInstrSubType::IsNaN:
          fmt::println("{:cd}", instr);
          fmt::println("{:cd}", *instr->get_parent()->get_parent().func);
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
      return true;
    }
    ASSERT(c1->is_float());
    const auto v1 = c1->as_float();
    const auto v2 = c2->as_float();

    bool is_true = false;
    // IMPORTANT: !!THIS IS IN OTHER SYNTAX SO FLIPPED ARGUMETNS!!
    // IMPORTANT: !!THIS IS IN OTHER SYNTAX SO FLIPPED ARGUMETNS!!
    switch (fcmp_subtype) {
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
        return true;
    }
    auto new_const_value = ctx->get_constant_int((u64)is_true, 8);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(ValueR(new_const_value));
    ASSERT(instr->bbs.size() == 0);
    instr.destroy();
    return true;
  }
  return false;
}

}  // namespace foptim::optim
