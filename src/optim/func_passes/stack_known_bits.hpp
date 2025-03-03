#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {

enum class StackOffsetResult {
  // could write anywhere on stack
  UnknownLocal,
  // we know where it writes
  KnownLocal,
  // we know it doesnt write to stack
  KnownNonLocal,
};
struct CachedKnowledge {
  StackOffsetResult result;
  u64 offset;
};

// using StackKnowCache = std::unordered_map<fir::ValueR, CachedKnowledge>;
// using StackKnowCache = TMap<fir::ValueR, CachedKnowledge>;
// TODO: WHYYYYYY DOES THIS WORK BUT NOT TMAP
using StackKnowCache = IRMap<fir::ValueR, CachedKnowledge>;

// NOTE:  the cache needs to already have all the allocas inserted
StackOffsetResult get_stack_offset(u64 &offset, fir::ValueR ptr,
                                   StackKnowCache &cache);

// if we got an alloca
// and we for example know the 0th byte of it always gets read and written as a
// i8 we can replace it with a extra alloca which can be lifted by mem2reg
//
// and
//
// calculates which bits of stack is known first propagate and calculate per bb
// entry/exit via dataflow and then do it localy inside each bb replacing loads
// if possible
// TODO: should be overwritten to handle each alloca on its own
class StackKnownBits final : public FunctionPass {
  void update_call(fir::Instr instr, utils::BitSet<> &new_in_one,
                   utils::BitSet<> &new_in_zero, StackKnowCache &cache);
  // returns false if the optimization failed
  bool update_store(fir::Instr instr, utils::BitSet<> &new_in_one,
                    utils::BitSet<> &new_in_zero, StackKnowCache &cache);
  void update_load(fir::Instr instr, utils::BitSet<> &new_in_one,
                   utils::BitSet<> &new_in_zero, StackKnowCache &cache,
                   TMap<fir::ValueR, i128> &known_load_values);

  void execute_sroa(TVec<fir::Instr> &load_stores, StackKnowCache &cache,
                    fir::BasicBlock entry_bb);

public:
  void apply(fir::Context &ctx, fir::Function &func) override;
};
} // namespace foptim::optim
