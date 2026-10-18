#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/types_ref.hpp"
#include "utils/types.hpp"

namespace foptim::optim {
void flip_cond_branch(fir::Instr cond_term);

struct GuessTypeResult {
  bool typeless;
  fir::TypeR type;
};

GuessTypeResult guessType(fir::ValueR ptr);

void swap_args(fir::Instr instr, u32 a1, u32 a2);

// zero and all ones constants are splatted with one instruction, hoisting
// them out of a loop only keeps a vector register busy
bool is_cheap_splat_constant(fir::ValueR v);

// recomputing the instruction where it is used costs about as much as keeping
// its result alive, so passes should not hoist or share it
bool is_cheap_to_rematerialize(fir::Instr instr);

struct IncomingData {
  fir::Instr term;
  u32 inc_bb_id;
};
// Given a bb with a conditional block at the end we can split it up into 2 bbs
bool cond_tail_duplication(foptim::fir::Context &ctx, foptim::optim::CFG &cfg,
                           foptim::optim::Dominators &dom,
                           fir::BasicBlock cond_bb, fir::Instr term,
                           IncomingData false_incoming);
}  // namespace foptim::optim
