#pragma once
#include "ir/function.hpp"
#include "ir/value.hpp"
#include "optim/analysis/mem/alias_sets.hpp"
#include "utils/map.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

/*
  The alias set S(v) of every pointer value of a function: the memory the
  pointer may point into, derived from how it is made.

    alloca                     own object under Stack, disjoint from Foreign
                               if it never escapes
    global                     own object under Globals (ReadOnlyGlobals if
                               constant), under PrivateGlobals if the address
                               never escapes (disjoint from Foreign)
    null / poison              Null
    argument                   Incoming (IncomingNonNull with nonnull, an
                               object under Restrict with noalias)
    noalias result of a call   own object under Heap, disjoint from Foreign if
                               it never escapes (then a union with Null unless
                               also nonnull), from Null if also nonnull
    load, call                 Foreign (also the alloca or heap object for a
                               load out of one that stores pointers into
                               itself)
    PtrAdd(p, index)           S(p), Unknown if p is null. Without inbounds
                               IncomingNonNull becomes Incoming
    select, block argument     union of the incoming sets, solved as a fixpoint
    bitcast of a pointer       S of the operand
    everything else            Unknown

  Pointers into the same object are never disjoint by set, the offsets decide
  those (mem_access.hpp). Two heap objects may get the same address once the
  first is freed, they are still disjoint because no access of the first may
  follow the free and the free call orders the accesses. One allocation call
  in a loop is the same set in every iteration. Cached by the AnalysisManager, dropped by every pass
  that changed anything.
*/
class Provenance {
 public:
  explicit Provenance(fir::Function &func);
  // knows constants only (null, globals), every other value is Unknown
  Provenance() : tab(AliasSetTable::roots()) {}

  // values created after construction are derived from their operands
  SetId set_of(fir::ValueR v);
  [[nodiscard]] const AliasSetTable &table() const { return tab; }
  // false once the module table grew
  [[nodiscard]] bool up_to_date(const fir::Function &func) const;

  bool disjoint(fir::ValueR a, fir::ValueR b) {
    return tab.disjoint(set_of(a), set_of(b));
  }
  bool non_null(fir::ValueR v) {
    return tab.disjoint(set_of(v), AliasSetTable::Null);
  }
  bool definitely_null(fir::ValueR v) {
    return tab.subset(set_of(v), AliasSetTable::Null);
  }
  bool is_stack(fir::ValueR v) {
    return tab.subset(set_of(v), AliasSetTable::Stack);
  }
  // heap memory or null
  bool is_heap(fir::ValueR v);
  bool is_global(fir::ValueR v) {
    const SetId s = set_of(v);
    return tab.subset(s, AliasSetTable::Globals) ||
           tab.subset(s, AliasSetTable::PrivateGlobals);
  }
  [[nodiscard]] bool escapes(fir::ValueR alloca) const;

  [[nodiscard]] bool equivalent(const Provenance &o) const;

 private:
  SetId constant_set(fir::ValueR v);
  SetId derive(fir::ValueR v, u32 depth);

  AliasSetTable tab;
  FDMap<fir::ValueR, SetId> sets;
  FDMap<fir::ValueR, SetId> const_sets;
  FDMap<fir::ValueR, bool> escaping;
};

}  // namespace foptim::optim
