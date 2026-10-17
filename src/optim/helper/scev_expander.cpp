#include "scev_expander.hpp"

#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/value.hpp"
#include "utils/todo.hpp"

namespace foptim::optim {

fir::ValueR SCEVExpander::expand(SCEVRef r) {
  if (auto it = cache.find(r); it != cache.end()) {
    return it->second;
  }
  ASSERT(evo.is_expandable(r));
  const SCEVExpr e = evo.expr(r);
  using T = SCEVExpr::Type;
  fir::ValueR res;
  switch (e.t) {
  case T::Const:
    res = fir::ValueR{ctx->get_constant_int(e.cval)};
    break;
  case T::Invariant:
    res = e.val;
    break;
  case T::Add: {
    // x + (y * -1) is how a subtraction is done
    const SCEVExpr &eb = evo.expr(e.b);
    if (eb.t == T::Mul && evo.is_const(eb.b) && evo.expr(eb.b).cval.is_all_ones()) {
      res = builder.build_int_sub(expand(e.a), expand(eb.a));
    } else {
      res = builder.build_int_add(expand(e.a), expand(e.b));
    }
    break;
  }
  case T::Mul:
    res = builder.build_int_mul(expand(e.a), expand(e.b));
    break;
  case T::SMax:
  case T::UMax:
    res = builder.build_intrinsic(expand(e.a), expand(e.b),
                                  e.t == T::SMax ? fir::IntrinsicSubType::SMax
                                                 : fir::IntrinsicSubType::UMax);
    break;
  case T::Cmp:
    res = builder.build_int_cmp(expand(e.a), expand(e.b),
                                static_cast<fir::ICmpInstrSubType>(e.pred));
    break;
  case T::Select: {
    const auto cond = expand(e.a);
    const auto t = expand(e.b);
    const auto f = expand(e.c);
    res = builder.build_select(t.get_type(), cond, t, f);
    break;
  }
  case T::SExt:
    res = builder.build_sext(expand(e.a), fir::TypeR{ctx->get_int_type(e.bw)});
    break;
  case T::ZExt:
    res = builder.build_zext(expand(e.a), fir::TypeR{ctx->get_int_type(e.bw)});
    break;
  case T::Trunc:
    res =
        builder.build_itrunc(expand(e.a), fir::TypeR{ctx->get_int_type(e.bw)});
    break;
  default:
    TODO("expression is not expandable");
  }
  cache.insert({r, res});
  return res;
}

} // namespace foptim::optim
