#pragma once
#include "ir/instruction.hpp"
#include "optim/analysis/mem/basic_alias_test.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

// Answer of a memory dependence query. Meant to grow (needs runtime check,
// conflicting) without changing the callers, so do not use a bool.
enum class MemDep : u8 {
  Independent,
  Dependent,
};

// The address and metadata part shared by BlockMemAccesses and LoopMemDep: can
// these two loads / stores touch the same bytes. With across_iterations a and
// b may be from different iterations of a loop, span names that loop (see
// IterSpan). The caller handles the case of two loads.
inline MemDep access_dep(AliasAnalyis &aa, fir::Instr a, fir::Instr b,
                         bool across_iterations = false,
                         const IterSpan *span = nullptr) {
  return aa.alias_access(a, b, across_iterations, span) ==
                 AliasAnalyis::AAResult::NoAlias
             ? MemDep::Independent
             : MemDep::Dependent;
}

}  // namespace foptim::optim
