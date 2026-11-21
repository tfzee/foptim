#include "dse.hpp"

#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "optim/analysis/mem/memory_ssa.hpp"

namespace foptim::optim {

namespace {

// accesses looked at per candidate, more makes it live
constexpr u32 MaxScan = 2048;

struct Candidate {
  MemLoc loc;
  const fir::AccessMeta *meta;
};

class DeadStores {
 public:
  explicit DeadStores(fir::Function &f)
      : func(&f),
        cfg(AnalysisManager::cfg(f)),
        dom(AnalysisManager::dom(f)),
        info(AnalysisManager::mem_accesses(f)),
        mssa(AnalysisManager::memory_ssa(f)),
        aq(AnalysisManager::provenance(f)) {}

  bool run() {
    TVec<fir::Instr> dead;
    for (u32 i = 0; i < info.accesses().size(); i++) {
      const auto &a = info.accesses()[i];
      if (!dom.is_reachable(cfg.get_bb_id(a.instr->get_parent()))) {
        continue;
      }
      const u32 id = mssa.access_of(a.instr);
      auto cand = candidate_of(a);
      if (id != MemorySSA::None && cand && is_dead(id, *cand)) {
        dead.push_back(a.instr);
      }
    }
    // the cached accesses name the dead instructions until here
    for (auto instr : dead) {
      instr.destroy();
    }
    return !dead.empty();
  }

 private:
  static std::optional<u32> intrin_bytes(fir::Instr instr) {
    if (!instr->args[2].is_constant_int()) {
      return std::nullopt;
    }
    const i128 unit =
        instr->is(fir::IntrinsicSubType::Memset)
            ? static_cast<i128>(instr->args[1].get_type()->get_size())
            : static_cast<i128>(instr->memcpy_elem_size());
    const i128 n = instr->args[2].as_constant()->as_int() * unit;
    if (n <= 0 || n >= UINT32_MAX) {
      return std::nullopt;
    }
    return static_cast<u32>(n);
  }

  // The bytes a plain store, memset or memcpy writes.
  std::optional<MemLoc> write_loc(const MemAccess &a) const {
    if (a.ordered) {
      return std::nullopt;
    }
    if (a.kind == MemAccess::Kind::Store && a.loc.size != 0) {
      return a.loc;
    }
    if (a.kind == MemAccess::Kind::BarrierRW && !a.instr->Volatile &&
        a.instr->is(fir::InstrType::Intrinsic) &&
        (a.instr->is(fir::IntrinsicSubType::Memset) ||
         a.instr->is(fir::IntrinsicSubType::Memcpy))) {
      auto bytes = intrin_bytes(a.instr);
      if (!bytes) {
        return std::nullopt;
      }
      MemLoc loc;
      loc.ptr = decompose_ptr(a.instr->args[0]);
      loc.size = *bytes;
      return loc;
    }
    return std::nullopt;
  }

  std::optional<Candidate> candidate_of(const MemAccess &a) const {
    auto loc = write_loc(a);
    if (!loc) {
      return std::nullopt;
    }
    const auto *meta =
        a.instr->has_access_meta ? func->get_access_meta(a.instr) : nullptr;
    return Candidate{*loc, meta};
  }

  static bool covers(const MemLoc &w, const MemLoc &l) {
    return w.ptr.same_base(l.ptr) && w.ptr.offset <= l.ptr.offset &&
           static_cast<i64>(l.ptr.offset + l.size) <=
               static_cast<i64>(w.ptr.offset + w.size);
  }

  // The block that defines the value, none for constants and globals.
  static std::optional<fir::BasicBlock> def_block(fir::ValueR v) {
    if (v.is_instr()) {
      return v.as_instr()->get_parent();
    }
    if (v.is_bb_arg()) {
      return v.as_bb_arg()->get_parent();
    }
    return std::nullopt;
  }

  // Entering a block that defines a value of the address makes the address
  // another one, the walk gives up there.
  bool redefines(u32 block, const MemLoc &loc) const {
    auto bb = cfg.bbrs[block].bb;
    if (def_block(loc.ptr.base) == bb) {
      return true;
    }
    for (const auto &t : loc.ptr.terms()) {
      if (def_block(t.index) == bb) {
        return true;
      }
    }
    return false;
  }

  // free of the start of the object the candidate writes into
  static bool frees_object(fir::Instr instr, const MemLoc &loc) {
    if (!instr->is(fir::InstrType::CallInstr) ||
        !instr->args[0].is_constant() ||
        !instr->args[0].as_constant()->is_func() ||
        instr->args[0].as_constant()->as_func()->attribs.libfunc !=
            fir::LibFunc::Free) {
      return false;
    }
    const auto shape = decompose_ptr(instr->args[1]);
    return shape.offset == 0 && !shape.has_terms() &&
           shape.same_base(whole_object(loc).ptr) && loc.ptr.offset >= 0;
  }

  bool dead_at_exit(const MemLoc &loc) {
    auto &prov = aq.provenance();
    const auto &tab = prov.table();
    const auto set = prov.set_of(loc.ptr.addr);
    if (tab.subset(set, AliasSetTable::Stack)) {
      return tab.disjoint(set, AliasSetTable::Foreign);
    }
    // a heap object nothing frees is never read again either, a store through
    // null is undefined so the null part of a maybe null allocation is ignored
    if (!prov.is_heap(loc.ptr.addr)) {
      return false;
    }
    if (!tab.is_union(set)) {
      return tab.disjoint(set, AliasSetTable::Foreign);
    }
    return std::ranges::all_of(tab.parts(set), [&](auto p) {
      return tab.subset(p, AliasSetTable::Null) ||
             tab.disjoint(p, AliasSetTable::Foreign);
    });
  }

  // The object a local candidate lives in, used where the address of the
  // candidate is another one than at the store.
  static MemLoc whole_object(const MemLoc &loc) {
    return MemLoc{.ptr = decompose_ptr(loc.ptr.base), .size = 0};
  }

  // After a phi of a block that defines a value of the address (the next
  // iteration of a loop) the address is another one, there only reads of the
  // whole object count and a kill does not.
  bool is_dead(u32 id, const Candidate &c) {
    const bool local = dead_at_exit(c.loc);
    const MemLoc whole = whole_object(c.loc);
    u32 budget = MaxScan;
    TSet<u64> visited;
    TVec<std::pair<u32, bool>> work;
    work.emplace_back(id, false);
    visited.insert(static_cast<u64>(id) << 1);
    while (!work.empty()) {
      const auto [state, crossed] = work.back();
      work.pop_back();
      if (!local && mssa.reaches_return(state)) {
        return false;
      }
      for (const u32 u : mssa.users(state)) {
        const auto &a = mssa[u];
        bool next = crossed;
        if (a.kind == MemorySSA::Kind::Phi) {
          next = crossed || redefines(a.block, c.loc);
          // another object in the next iteration, nothing to say about it
          if (next && def_block(c.loc.ptr.base) == cfg.bbrs[a.block].bb) {
            return false;
          }
          if (visited.insert(static_cast<u64>(u) << 1 | next).second) {
            work.emplace_back(u, next);
          }
          continue;
        }
        if (budget-- == 0) {
          return false;
        }
        if (frees_object(a.instr, c.loc)) {
          continue;
        }
        const auto mr = aq.mod_ref(a.instr, crossed ? whole : c.loc, c.meta);
        if (may_read(mr)) {
          return false;
        }
        if (a.kind != MemorySSA::Kind::Def) {
          continue;
        }
        if (!crossed && may_write(mr)) {
          const auto *acc = info.find(a.instr);
          const auto w = acc != nullptr ? write_loc(*acc) : std::nullopt;
          if (w && covers(*w, c.loc)) {
            continue;
          }
        }
        if (visited.insert(static_cast<u64>(u) << 1 | crossed).second) {
          work.emplace_back(u, crossed);
        }
      }
    }
    return true;
  }

  fir::Function *func;
  CFG &cfg;
  Dominators &dom;
  MemAccessInfo &info;
  MemorySSA &mssa;
  AliasQuery aq;
};

}  // namespace

PreservedAnalysis DSE::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedNC("DSE", COLOR_OPTIMF);
  (void)ctx;
  // the memory accesses are only worth building if something writes
  const bool has_write =
      std::ranges::any_of(func.basic_blocks, [](fir::BasicBlock bb) {
        return std::ranges::any_of(bb->get_instrs(), [](fir::Instr i) {
          return i->is(fir::InstrType::StoreInstr) ||
                 i->is(fir::IntrinsicSubType::Memset) ||
                 i->is(fir::IntrinsicSubType::Memcpy);
        });
      });
  if (!has_write) {
    return PreservedAnalysis::all();
  }
  return DeadStores{func}.run() ? PreservedAnalysis::cfg_only()
                                : PreservedAnalysis::all();
}

}  // namespace foptim::optim
