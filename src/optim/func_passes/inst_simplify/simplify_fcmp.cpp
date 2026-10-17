#include "simplify_fcmp.hpp"

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim::InstSimp {

namespace {
void swap_args_fcmp(fir::Instr instr) {
  ASSERT(instr->is(fir::InstrType::FCmp))
  using fir::FCmpInstrSubType;
  // a < b == b > a, the symmetric predicates keep theirs
  const auto flip = [&instr](FCmpInstrSubType to) {
    instr->subtype = static_cast<u32>(to);
    swap_args(instr, 0, 1);
  };
  switch (static_cast<FCmpInstrSubType>(instr->subtype)) {
    case FCmpInstrSubType::INVALID:
    case FCmpInstrSubType::AlwFalse:
    case FCmpInstrSubType::OEQ:
    case FCmpInstrSubType::UEQ:
    case FCmpInstrSubType::ONE:
    case FCmpInstrSubType::UNE:
    case FCmpInstrSubType::ORD:
    case FCmpInstrSubType::UNO:
    case FCmpInstrSubType::AlwTrue:
      swap_args(instr, 0, 1);
      break;
    case FCmpInstrSubType::OGT:
      flip(FCmpInstrSubType::OLT);
      break;
    case FCmpInstrSubType::OGE:
      flip(FCmpInstrSubType::OLE);
      break;
    case FCmpInstrSubType::OLT:
      flip(FCmpInstrSubType::OGT);
      break;
    case FCmpInstrSubType::OLE:
      flip(FCmpInstrSubType::OGE);
      break;
    case FCmpInstrSubType::UGT:
      flip(FCmpInstrSubType::ULT);
      break;
    case FCmpInstrSubType::UGE:
      flip(FCmpInstrSubType::ULE);
      break;
    case FCmpInstrSubType::ULT:
      flip(FCmpInstrSubType::UGT);
      break;
    case FCmpInstrSubType::ULE:
      flip(FCmpInstrSubType::UGE);
      break;
    case FCmpInstrSubType::IsNaN:
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

f64 const_fp(fir::ConstantValueR c) {
  return c->get_type()->as_float() == 32 ? static_cast<f64>(c->as_f32())
                                         : c->as_f64();
}

namespace {
bool simplify_fcmp_impl(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                        WorkList &worklist);
}  // namespace

bool simplify_fcmp(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                   WorkList &worklist) {
  bool normalized = false;
  if ((instr->args[0].is_constant() &&
       (!instr->args[0].as_constant()->is_global() &&
        !instr->args[0].as_constant()->is_func())) &&
      (!instr->args[1].is_constant() ||
       (instr->args[1].as_constant()->is_global() ||
        instr->args[1].as_constant()->is_func()))) {
    const auto before = instr->args[0];
    swap_args_fcmp(instr);
    normalized = instr->args[0] != before;
  }
  return simplify_fcmp_impl(instr, bb, ctx, worklist) || normalized;
}

namespace {
bool simplify_fcmp_impl(fir::Instr instr, fir::BasicBlock /*bb*/,
                        fir::Context &ctx, WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyFCMP");
  }
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

  bool second_constant = instr->args[1].is_constant();
  auto fcmp_subtype =
      static_cast<fir::FCmpInstrSubType>(instr->get_instr_subtype());

  // conert to integer comparison
  if (instr->args[0].is_instr() && second_constant) {
    auto arg0_i = instr->args[0].as_instr();
    auto const1_i = instr->args[1].as_constant();
    if (const1_i->is_float() && arg0_i->is(fir::ConversionSubType::FPEXT)) {
      auto const_val = const1_i->as_f64();
      if (arg0_i->args[0].get_type()->as_float() == 32 &&
          arg0_i.result_type()->as_float() == 64 &&
          canNarrowFpCompare(const_val, fcmp_subtype)) {
        fir::Builder buh{instr};
        auto new_cmp = buh.build_float_cmp(
            arg0_i->args[0],
            fir::ValueR{ctx->get_constant_value(static_cast<f32>(const_val),
                                                ctx->get_float_type(32))},
            fcmp_subtype);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_cmp);
        instr.destroy();
        if (arg0_i->get_n_uses() == 1) {
          arg0_i.destroy();
        }
        return true;
      }
    } else if (const1_i->is_float() &&
               arg0_i->is(fir::ConversionSubType::SITOFP)) {
      fir::Builder b{instr};
      // TODO: this is iffy
      // will prob fail for very big constants and or very big values that both
      // have problems in getting represented in the other
      auto const_val = const1_i->as_f64();

      switch (fcmp_subtype) {
        case fir::FCmpInstrSubType::OLT: {
          auto r = b.build_int_cmp(arg0_i->args[0],
                                   fir::ValueR{ctx->get_constant_value(
                                       static_cast<i128>(ceil(const_val)),
                                       arg0_i->args[0].get_type())},
                                   fir::ICmpInstrSubType::SLT);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(r);
          instr.destroy();
          return true;
        }
        case fir::FCmpInstrSubType::OGT: {
          auto r = b.build_int_cmp(arg0_i->args[0],
                                   fir::ValueR{ctx->get_constant_value(
                                       static_cast<i128>(floor(const_val)),
                                       arg0_i->args[0].get_type())},
                                   fir::ICmpInstrSubType::SGT);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(r);
          instr.destroy();
          return true;
        }
        case fir::FCmpInstrSubType::UNE: {
          auto r = b.build_int_cmp(arg0_i->args[0],
                                   fir::ValueR{ctx->get_constant_value(
                                       static_cast<i128>(floor(const_val)),
                                       arg0_i->args[0].get_type())},
                                   fir::ICmpInstrSubType::NE);
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
          break;
      }
    }
  }

  return false;
}
}  // namespace

}  // namespace foptim::optim::InstSimp
