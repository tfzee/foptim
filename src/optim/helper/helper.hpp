#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "ir/types_ref.hpp"
#include "utils/set.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

// Poison here would taint everything it is combined with (and x, 0 is not
// poison for undef), so take zero where the type has one.
inline fir::ValueR uninit_load_value(fir::ContextData *ctx, fir::TypeR ty) {
  if (ty->is_float()) {
    return fir::ValueR{ctx->get_constant_value(0.0, ty)};
  }
  if (ty->is_int()) {
    return fir::ValueR{ctx->get_constant_value(0, ty)};
  }
  if (ty->is_ptr()) {
    return fir::ValueR{ctx->get_constant_null()};
  }
  return fir::ValueR{ctx->get_poisson_value(ty)};
}
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

// Allocas, globals, defined functions, inbounds offsets and selects of those
// and nonnull arguments.
bool is_known_nonnull(fir::ValueR v, u32 depth = 4);

// Functions referenced from a global initializer, not in their use lists.
TSet<const fir::Function *> functions_referenced_by_globals(fir::Context &ctx);

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
