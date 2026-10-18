#pragma once
#include <optional>

#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/string.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

// A single entry, closed set of blocks of one function. Every edge from the
// outside goes to the entry and every successor of a block is in the region,
// so the paths end in ret, unreachable or a call that does not return. That
// means no value lives out and the region can become a function.
struct Region {
  enum class Kind : u8 { Returns, NoReturn };
  fir::BasicBlock entry{fir::BasicBlock::invalid()};
  // entry first, function order otherwise
  TVec<fir::BasicBlock> blocks;
  // outside values used inside, in order of first use
  TVec<fir::ValueR> live_in;
  Kind kind = Kind::Returns;
  size_t n_instrs = 0;
};

enum class RegionReject : u8 {
  None,
  FunctionEntry,
  SecondEntry,
  LiveOut,
  DynamicAlloca,
  VarArgs,
  TooSmall,
  TooManyLiveIns,
  PromotableAlloca,
};

struct RegionLimits {
  size_t min_instrs = 0;
  size_t max_live_ins = 8;
};

struct ExtractAttribs {
  bool cold = false;
  bool no_inline = true;
};

// Read only. Returns nullopt when da closure of entry can not be
// moved into a function, the reason is stored in why.
std::optional<Region> find_region(fir::Function &f, fir::BasicBlock entry,
                                  const CFG &cfg, const RegionLimits &limits,
                                  RegionReject *why = nullptr);
IRString unique_function_name(fir::ContextData &ctx, const IRString &base);
// Moves the region into the new function name. The new function has the
// parameters of the entry block arguments followed by the live ins. In f the
// region entry stays and is rewritten to call the new function followed by
// ret / unreachable. Returns the call.
fir::Instr extract_region(fir::Function &f, const Region &r, IRString name,
                          ExtractAttribs attribs);

}  // namespace foptim::optim
