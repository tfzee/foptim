#pragma once
#include "ir/instruction.hpp"
#include "ir/types_ref.hpp"

namespace foptim::optim {
void flip_cond_branch(fir::Instr cond_term);

struct GuessTypeResult {
  bool typeless;
  fir::TypeR type;
};

GuessTypeResult guessType(fir::ValueR ptr);

}  // namespace foptim::optim
