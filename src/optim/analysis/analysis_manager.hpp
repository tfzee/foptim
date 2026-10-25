#pragma once
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "ir/function.hpp"
#include "ir/global.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/constraint_analysis.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "optim/analysis/mem/memory_ssa.hpp"
#include "optim/analysis/mem/provenance.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

// The analyses that are cached on a function. Bit flags so a pass can say
// which of them stay valid.
enum class AnalysisKind : u32 {
  CFG = 1U << 0,
  // needs the CFG, so it is dropped whenever the CFG is
  Dominators = 1U << 1,
  // needs the CFG and refers to instructions (their ValueRs), so any pass
  // that touches instructions has to drop it
  Constraints = 1U << 2,
  // refers to instructions and their operands, so like Constraints any pass
  // that touches instructions has to drop it, cfg_only() does not keep it
  MemAccesses = 1U << 3,
  // alias sets of the pointer values, same rule as MemAccesses
  Provenance = 1U << 4,
  // the memory chain, refers to instructions and block ids of the CFG, same
  // rule as MemAccesses
  MemorySSA = 1U << 5,
};

struct PreservedAnalysis {
  u32 mask = 0;

  static PreservedAnalysis all() { return PreservedAnalysis{.mask = ~0U}; }
  static PreservedAnalysis none() { return PreservedAnalysis{.mask = 0}; }
  // The block structure (blocks and their edges) was not touched, but
  // instructions may have changed.
  static PreservedAnalysis cfg_only() {
    return PreservedAnalysis{.mask =
                                 static_cast<u32>(AnalysisKind::CFG) |
                                 static_cast<u32>(AnalysisKind::Dominators)};
  }
  [[nodiscard]] bool preserves(AnalysisKind k) const {
    return (mask & static_cast<u32>(k)) != 0;
  }
};

// Lives on the function (fir::Function::analysis_cache) so there is no
// function -> analysis map to keep in sync when functions are created or
// erased, and no sharing between the jobs of different functions.
struct FunctionAnalysisCache {
  std::unique_ptr<CFG> cfg;
  std::unique_ptr<Dominators> dom;
  std::unique_ptr<ConstraintAnalysis> constraints;
  std::unique_ptr<MemAccessInfo> mem_accesses;
  std::unique_ptr<Provenance> provenance;
  std::unique_ptr<MemorySSA> memory_ssa;
};

/*
Caches the analyses per function and drops them according to the
PreservedAnalysis a pass returns.

Thread safety: a function's cache is only touched by the job that currently
owns that function. Function passes run one job per function, module passes
must only call into the manager for functions they own in the current job (or
from their serial part). The manager has no shared state.

The returned references stay valid until the analysis is invalidated, i.e.
until the pass returns (or until the pass itself reassigns the object).
A pass that changes the IR in a way that makes a cached analysis wrong has to
return the matching PreservedAnalysis, in assert builds this is checked by
verify_preserved.

Not cached yet (still built by each user, candidates in this order):
 - LiveVariables, LoopInfoAnalysis (+ DominatorTree), ScalarEvo (per loop)
 - AllocaEscape, AccessAnalysis
 - CallGraph (module level, not per function)
 - AttributerManager / KnownBits (instruction level, cheap to invalidate on
   every change but expensive to rebuild)
*/
class AnalysisManager {
  // module pass reporting, see touched_body
  std::mutex report_mutex;
  // assert builds: what the module pass reported and what it looked like before
  std::unordered_set<const fir::Function *> reported_bodies;
  std::unordered_set<const fir::Function *> reported_interfaces;
  struct Snapshot {
    u64 body;
    u64 interface;
  };
  std::unordered_map<const fir::Function *, Snapshot> before_module_pass;

 public:
  static CFG &cfg(fir::Function &func);
  static Dominators &dom(fir::Function &func);
  static ConstraintAnalysis &constraints(fir::Function &func);
  static MemAccessInfo &mem_accesses(fir::Function &func);
  static Provenance &provenance(fir::Function &func);
  static MemorySSA &memory_ssa(fir::Function &func);

  // A pass that moves, creates or destroys instructions while it runs drops the
  // memory analyses (accesses, memory SSA, provenance) it asked for before it
  // asks again: they are keyed by values, and the slot of a destroyed one can
  // be given to a new instruction. Everything else stays.
  static void drop_memory(fir::Function &func);

  // drop everything the pass did not preserve
  static void invalidate(fir::Function &func, PreservedAnalysis pres);
  /*
  Module passes tell what they changed, so the function analyses and the "this
  pass is a no-op on the function" bits (pass_skipping.hpp) of everything else
  survive. Call these AFTER making the change, from any thread.
  A module pass that returns PreservedAnalysis::all() promises that everything
  it changed on the functions that still exist was reported (builds with
  -Dhash_checks=ON check this by hashing every function before and after). Any
  other return value means the pass did not report and everything is dropped.
  */
  // instructions or blocks of f changed: drops f's analyses and bits
  void touched_body(fir::Function &f);
  // attributes, signature or decl-ness of g changed: drops the bits of g and of
  // every function calling it (passes read callee attributes everywhere)
  void touched_interface(fir::Function &g);
  // constness or initializer of a global changed: drops the bits of its users
  void touched_global(fir::Global g);
  // the bodies of all functions that contain a call to f changed (arguments or
  // results of the calls were rewritten)
  void touched_callers(fir::Function &f);
  // f got a new signature (arguments / return type removed, promoted, ...):
  // its body and interface changed and every call site was rewritten
  void touched_signature(fir::Function &f);

  // called by the ModulePassManager around each module pass
  void begin_module_pass(fir::Context &ctx);
  // returns whether the pass reported its changes (so only those were dropped)
  bool end_module_pass(fir::Context &ctx, PreservedAnalysis result,
                       std::string_view pass_name);

  static u64 hash_interface(const fir::Function &func);

  // drop all cached analyses of every function (used after module passes), this
  // also forgets which function passes were known to be a no-op on a function
  static void invalidate_all(fir::Context &ctx);

  // assert builds only: recompute what the pass claimed to preserve and
  // compare it to the cache
  static bool verify_preserved(fir::Function &func, PreservedAnalysis pres);

  // hash over everything a pass can change in the function (blocks,
  // instructions, operands, flags), values are hashed by identity
  static u64 hash_function(const fir::Function &func);
  // the same for a single instruction (operands, flags, targets)
  static u64 hash_instr(fir::Instr instr);
  // a pass that returned PreservedAnalysis::all() claims it did not touch the
  // function, so the hash taken before it ran has to still match
  static bool verify_unchanged(const fir::Function &func, u64 hash_before,
                               u64 hash_after, std::string_view pass_name);
};

}  // namespace foptim::optim
