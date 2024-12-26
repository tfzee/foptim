#pragma once
#include "mir/matcher.hpp"

namespace foptim::fmir {

void memory_patterns(IRVec<Pattern> &pats);
void cjmp_patterns(IRVec<Pattern> &pats);
void base_patterns(IRVec<Pattern> &pats);
void arith_patterns(IRVec<Pattern> &pats);

inline auto get_pats() {
  IRVec<Pattern> res;
  res.reserve(100);

  // memory_patterns(res);
  // cjmp_patterns(res);
  // arith_patterns(res);
  base_patterns(res);

  return res;
}
} // namespace foptim::fmir
