#pragma once
#include <string>

#include "ir/access_meta.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "utils/map.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

class MemorySSAWalker;

/*
  One chain of memory states per function. An instruction that may write
  memory is a Def producing a new state, one that only reads is a Use
  of a state, a Phi joins the states of the predecessors where different ones
  meet. State 0 is the memory on entry. Access ids index accesses(), the order
  is not the program order. Which def a use really depends on is decided by the
  MemorySSAWalker. Cached by the AnalysisManager, dropped by every pass that
  changed anything.
*/
class MemorySSA {
 public:
  enum class Kind : u8 { LiveOnEntry, Def, Use, Phi, Removed };
  static constexpr u32 LiveOnEntry = 0;
  static constexpr u32 None = ~0U;

  struct Access {
    Kind kind = Kind::LiveOnEntry;
    // Load / Store / Barrier of the instruction, Def and Use only
    MemAccess::Kind access_kind = MemAccess::Kind::Load;
    // a volatile or atomic access, the walker never looks past it
    bool ordered = false;
    // block id in the CFG, none for LiveOnEntry
    u32 block = None;
    fir::Instr instr;
    // Def and Use: the state before the access
    u32 defining = None;
    // Phi: operands[ops_begin + i] is the state coming from pred i of the block
    u32 ops_begin = 0;
    u32 n_ops = 0;
  };

  MemorySSA(const CFG &cfg, const Dominators &dom, const MemAccessInfo &info);

  [[nodiscard]] const FVec<Access> &accesses() const { return acc; }
  [[nodiscard]] const Access &operator[](u32 id) const { return acc[id]; }
  [[nodiscard]] u32 n_accesses() const { return static_cast<u32>(acc.size()); }

  // id of the Def / Use of a load, store, call, ..., None for instructions
  // that do not touch memory
  [[nodiscard]] u32 access_of(fir::Instr instr) const;
  // the state right before the instruction (the id of the def above it, a phi
  // or LiveOnEntry), None for instructions that do not touch memory
  [[nodiscard]] u32 defining_access(fir::Instr instr) const;
  // the phi at the start of the block (CFG id), None if there is none
  [[nodiscard]] u32 phi_of(u32 block) const { return block_phi[block]; }
  [[nodiscard]] u32 n_operands(u32 phi) const { return acc[phi].n_ops; }
  [[nodiscard]] u32 operand(u32 phi, u32 i) const {
    return operands[acc[phi].ops_begin + i];
  }

  // The structure follows the rules (every use is below a def that dominates
  // it, one phi operand per predecessor, no phi that merges one state).
  [[nodiscard]] bool verify(const CFG &cfg, const Dominators &dom) const;
  // same accesses with the same defining accesses (for the cache verification)
  [[nodiscard]] bool equivalent(const MemorySSA &o) const;

  // One line per access, blocks in CFG order. With a walker every load also
  // shows the def it really depends on.
  [[nodiscard]] std::string to_string(const CFG &cfg,
                                      MemorySSAWalker *walker = nullptr) const;

 private:
  void remove_trivial_phis();

  FVec<Access> acc;
  FVec<u32> operands;
  // block id -> phi
  FVec<u32> block_phi;
  FDMap<fir::Instr, u32> index;
};

/*
  Which def a load really depends on: the walk goes up from an access and
  skips every def the alias layer proves does not touch the location. Phis are
  followed through all operands, different clobbers make the phi itself the
  answer. After MaxSteps accesses it returns the access it stopped at, so the
  answer is always conservative. Nothing is cached across queries.
*/
class MemorySSAWalker {
 public:
  static constexpr u32 MaxSteps = 256;

  MemorySSAWalker(const MemorySSA &mssa, const MemAccessInfo &info,
                  AliasQuery &aq, u32 max_steps = MaxSteps)
      : mssa(&mssa), info(&info), aq(&aq), max_steps(max_steps) {}

  // The nearest access above the load / use that may write what it reads
  // (a Def, a Phi or LiveOnEntry). Volatile / atomic accesses and calls that
  // read unknown memory are not looked past: their immediate state is the
  // answer.
  u32 clobber(fir::Instr use);

  // The same for a location and the state to start at. want says what counts
  // as a clobber, Mod for a load, Ref | Mod for a store that wants to know if
  // anything reads or writes the bytes before it. stop (default None) is a
  // state the walk does not go beyond, reaching it on a path is not a
  // clobber and returns it.
  u32 clobber_from(u32 start, const MemLoc &loc,
                   const fir::AccessMeta *meta = nullptr,
                   ModRef want = ModRef::Mod, u32 stop = MemorySSA::None);

  // Can anything on the way from the state "from" to the state "to" (included,
  // from excluded) write loc? "from" has to dominate "to", otherwise the answer
  // is true. Gives up with true after MaxSteps.
  bool clobbered_between(u32 from, u32 to, const MemLoc &loc,
                         const fir::AccessMeta *meta = nullptr,
                         ModRef want = ModRef::Mod);

  // number of accesses of the last query that were looked at
  [[nodiscard]] u32 last_steps() const { return steps; }

 private:
  struct Query {
    const MemLoc *loc;
    const fir::AccessMeta *meta;
    ModRef want;
    u32 stop;
    // phis being walked, innermost last
    TVec<u32> active;
    // finished phis whose result does not depend on a phi above them
    TMap<u32, u32> done;
    // the outermost active phi (index in active) that a cycle led back to
    u32 cycle_to = ~0U;
  };
  // None if the walk only reached phis that are on the way already
  u32 walk(u32 a, Query &q);

  const MemorySSA *mssa;
  const MemAccessInfo *info;
  AliasQuery *aq;
  u32 max_steps;
  u32 steps = 0;
};

}  // namespace foptim::optim
