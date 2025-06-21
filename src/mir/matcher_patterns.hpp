#pragma once
#include "utils/vec.hpp"

namespace foptim::fmir {

struct Pattern;

void memory_patterns(IRVec<Pattern> &pats);
void cjmp_patterns(IRVec<Pattern> &pats);
void move_patterns(IRVec<Pattern> &pats);
void base_patterns(IRVec<Pattern> &pats);
void arith_patterns(IRVec<Pattern> &pats);
void intrin_patterns(IRVec<Pattern> &pats);

IRVec<Pattern> get_pats();
} // namespace foptim::fmir
