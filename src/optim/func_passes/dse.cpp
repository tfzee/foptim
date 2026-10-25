#include "dse.hpp"

#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/mem_access.hpp"

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
        aq(AnalysisManager::provenance(f)) {}

  bool run() {
    index_blocks();
    TVec<fir::Instr> dead;
    for (u32 i = 0; i < info.accesses().size(); i++) {
      const auto &a = info.accesses()[i];
      if (!dom.is_reachable(cfg.get_bb_id(a.instr->get_parent()))) {
        continue;
      }
      auto cand = candidate_of(a);
      if (cand && is_dead(i, *cand)) {
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
  enum class Walk : u8 { Continue, Live, Killed };

  // [begin, end) of the accesses of every block (by CFG id)
  void index_blocks() {
    range.assign(cfg.bbrs.size(), {0, 0});
    const auto &accs = info.accesses();
    block_of.resize(accs.size());
    for (u32 i = 0; i < accs.size();) {
      const u32 b = cfg.get_bb_id(accs[i].instr->get_parent());
      u32 j = i;
      while (j < accs.size() &&
             accs[j].instr->get_parent() == accs[i].instr->get_parent()) {
        block_of[j] = b;
        j++;
      }
      range[b] = {i, j};
      i = j;
    }
  }

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

  Walk scan(u32 from, u32 to, const Candidate &c, u32 &budget) {
    const auto &accs = info.accesses();
    for (u32 i = from; i < to; i++) {
      if (budget == 0) {
        return Walk::Live;
      }
      budget--;
      const auto &x = accs[i];
      const auto mr = aq.mod_ref(x.instr, c.loc, c.meta);
      if (may_read(mr)) {
        return Walk::Live;
      }
      if (may_write(mr)) {
        auto w = write_loc(x);
        if (w && covers(*w, c.loc)) {
          return Walk::Killed;
        }
      }
    }
    return Walk::Continue;
  }

  bool dead_at_exit(const MemLoc &loc) {
    const auto &tab = aq.provenance().table();
    const auto set = aq.provenance().set_of(loc.ptr.addr);
    return tab.subset(set, AliasSetTable::Stack) &&
           tab.disjoint(set, AliasSetTable::Foreign);
  }

  bool is_dead(u32 id, const Candidate &c) {
    const u32 start_block = block_of[id];
    u32 budget = MaxScan;
    switch (scan(id + 1, range[start_block].second, c, budget)) {
      case Walk::Live:
        return false;
      case Walk::Killed:
        return true;
      case Walk::Continue:
        break;
    }
    const bool local = dead_at_exit(c.loc);
    TVec<u32> work;
    TSet<u32> visited;
    work.push_back(start_block);
    bool first = true;
    while (!work.empty()) {
      const u32 b = work.back();
      work.pop_back();
      // the rest of the start block was scanned already
      if (!first) {
        if (!visited.insert(b).second) {
          continue;
        }
        if (redefines(b, c.loc)) {
          return false;
        }
        switch (scan(range[b].first, range[b].second, c, budget)) {
          case Walk::Live:
            return false;
          case Walk::Killed:
            continue;
          case Walk::Continue:
            break;
        }
      }
      first = false;
      const auto &succ = cfg.bbrs[b].succ;
      if (succ.empty()) {
        auto term = cfg.bbrs[b].bb->get_terminator();
        if (term->is(fir::InstrType::ReturnInstr) && !local) {
          return false;
        }
        continue;
      }
      for (u32 s : succ) {
        work.push_back(s);
      }
    }
    return true;
  }

  fir::Function *func;
  CFG &cfg;
  Dominators &dom;
  MemAccessInfo &info;
  AliasQuery aq;
  TVec<std::pair<u32, u32>> range;
  TVec<u32> block_of;
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
