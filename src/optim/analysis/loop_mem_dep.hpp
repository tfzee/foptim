#pragma once
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/block_mem_access.hpp"
#include "optim/analysis/loop_analysis.hpp"

namespace foptim::optim {

// Can two accesses of a loop run for VF iterations at once, with every
// statement as one vector operation. This is the loop version of
// BlockMemAccesses can_sink and gives the same MemDep answer.
class LoopMemDep {
 public:
  struct Access {
    fir::ValueR ptr;
    // start plus step per iteration, if the address is affine
    SCEVRef addr = InvalidSCEV;
    u32 size = 0;
    bool is_store = false;
  };

  LoopMemDep(ScalarEvo &evo, AliasAnalyis &aa) : evo(evo), aa(aa) {}

  [[nodiscard]] MemDep query(const Access &a, const Access &b, u32 vf);

 private:
  ScalarEvo &evo;
  AliasAnalyis &aa;
};

inline MemDep LoopMemDep::query(const Access &a, const Access &b, u32 vf) {
  if (!a.is_store && !b.is_store) {
    return MemDep::Independent;
  }
  if (aa.alias(a.ptr, b.ptr, a.size, b.size) ==
      AliasAnalyis::AAResult::NoAlias) {
    return MemDep::Independent;
  }
  if (a.addr == InvalidSCEV || b.addr == InvalidSCEV) {
    return MemDep::Dependent;
  }
  const auto &ea = evo.expr(a.addr);
  const auto &eb = evo.expr(b.addr);
  if (ea.t != SCEVExpr::Type::AddRec || eb.t != SCEVExpr::Type::AddRec ||
      !evo.is_const(ea.b) || !evo.is_const(eb.b) ||
      evo.expr(ea.b).cval_i() != evo.expr(eb.b).cval_i()) {
    return MemDep::Dependent;
  }
  const i128 step = evo.expr(ea.b).cval_i();
  if (step <= 0 || step < static_cast<i128>(a.size) || a.size != b.size) {
    return MemDep::Dependent;
  }
  if (evo.same_expr(ea.a, eb.a)) {
    return MemDep::Independent;
  }
  // b - a at the same iteration, has to be a constant
  const auto minus_one = evo.mk_const(-1, evo.expr(ea.a).bw);
  const auto diff = evo.mk_add(eb.a, evo.mk_mul(minus_one, ea.a));
  if (!evo.is_const(diff)) {
    return MemDep::Dependent;
  }
  const i128 d = evo.expr(diff).cval_i();
  if (d == 0) {
    return MemDep::Independent;
  }
  if (d % step != 0) {
    // partial overlap of different iterations
    return MemDep::Dependent;
  }
  const i128 dist = (d < 0 ? -d : d) / step;
  // conflicting iterations at least vf apart never share a vector iteration
  return dist >= static_cast<i128>(vf) ? MemDep::Independent
                                       : MemDep::Dependent;
}

}  // namespace foptim::optim
