#pragma once
#include "../function_pass.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/alloca_escape.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/set.hpp"
#include <llvm/IR/Instructions.h>

namespace foptim::optim {

// detect dead stores and also dead memcpy/memset
class DSE final : public FunctionPass {
public:
  struct Config {};
  Config config;

  struct MemWrite {
    fir::Instr instr;
    fir::ValueR ptr;
    fir::ValueR size;
    u64 static_size = 0;
  };

  bool is_dead_store_candidate(fir::Instr i, MemWrite &out) {
    switch (i->instr_type) {
    case fir::InstrType::StoreInstr: {
      out.instr = i;
      out.ptr = i->args[0];
      out.size = fir::ValueR{};
      out.static_size = i->get_type()->get_size();
      return true;
    }
    case fir::InstrType::CallInstr: {
      if (i->args[0].is_constant_func()) {
        auto name = i->args[0].as_constant()->as_func()->name;
        if (name == "foptim.memcpy" || name == "foptim.memset") {
          out.instr = i;
          out.ptr = i->args[1];  // dst
          out.size = i->args[3]; // len
          out.static_size = 0;
          return true;
        }
      }
      return false;
    }
    default:
      return false;
    }
  }

  void collect_writes_to_alloca(fir::ValueR alloca_val, TSet<fir::Instr> &seen,
                                TVec<MemWrite> &out) {
    for (auto &use : *alloca_val.get_uses()) {
      auto user = use.user;
      if (seen.contains(user)) {
        continue;
      }
      seen.insert(user);

      MemWrite w;
      if (is_dead_store_candidate(fir::Instr{user}, w)) {
        // dst position differs by instr kind: Store's dst is argId 0,
        // memcpy/memset's dst is argId 1. Only collect this use as a write if
        // it's actually the destination-operand use, not an incidental one
        bool is_dest_use =
            (user->is(fir::InstrType::StoreInstr) && use.argId == 0) ||
            (user->is(fir::InstrType::CallInstr) && use.argId == 1);
        if (is_dest_use) {
          out.push_back(w);
        }
      }
      if (user->is(fir::BinaryInstrSubType::PtrAdd) ||
          user->is(fir::BinaryInstrSubType::IntAdd) ||
          user->is(fir::ConversionSubType::PtrToInt)) {
        collect_writes_to_alloca(fir::ValueR{user}, seen, out);
      }
    }
  }

  bool eliminate_dead_alloca_stores(fir::Context &ctx, fir::Function &func,
                                    Dominators &dom, CFG &cfg,
                                    AliasAnalyis &aa) {
    (void)ctx;
    (void)dom;
    (void)cfg;
    bool changed = false;

    // Byte range [off, off+size) written/read, relative to the alloca base.
    struct Range {
      bool unknown = false;
      bool whole_object = false;
      u64 off = 0, size = 0;

      [[nodiscard]] bool overlaps(const Range &o) const {
        if (whole_object || unknown || o.unknown) {
          return true;
        }
        return off < o.off + o.size && o.off < off + size;
      }

      [[nodiscard]] bool covers(const Range &o) const {
        if (whole_object) {
          return true;
        }
        if (unknown || o.unknown) {
          return false;
        }
        return off <= o.off && o.off + o.size <= off + size;
      }
    };

    auto get_range = [&](fir::ValueR ptr, fir::ValueR base, u64 size) -> Range {
      // FIX: Check for unknown size explicitly to prevent ~0ULL wrap-arounds
      if (size == ~0ULL) {
        return {.unknown = true, .off = 0, .size = 0};
      }
      if (ptr == base) {
        return {.unknown = false, .off = 0, .size = size};
      }
      if (ptr.is_instr() &&
          (ptr.as_instr()->is(fir::BinaryInstrSubType::IntAdd) ||
           ptr.as_instr()->is(fir::BinaryInstrSubType::PtrAdd))) {
        auto iptr = ptr.as_instr();
        fir::ValueR a = iptr->args[0];
        fir::ValueR b = iptr->args[1];
        if (a == base && b.is_constant_int()) {
          return {.unknown = false,
                  .off = static_cast<u64>(b.as_constant()->as_int()),
                  .size = size};
        }
        if (b == base && a.is_constant_int()) {
          return {.unknown = false,
                  .off = static_cast<u64>(a.as_constant()->as_int()),
                  .size = size};
        }
      }
      // If we can't definitively trace the exact offset, it's an unknown write
      // range
      return {.unknown = true, .off = 0, .size = 0};
    };

    for (auto &bb : func.basic_blocks) {
      for (auto &instr : bb->instructions) {
        if (!instr->is(fir::InstrType::AllocaInstr)) {
          continue;
        }
        fir::ValueR alloca_val{instr};

        auto escape_info = analyze_alloca_escape(alloca_val);
        if (escape_info.escapes) {
          continue;
        }
        // Pointers read back out of the alloca (self stores) alias it in ways
        // the alias analysis can not see, so reads through them are reads of
        // the alloca.
        auto reads_alloca = [&](fir::ValueR ptr) {
          return escape_info.derived.contains(ptr);
        };

        TSet<fir::Instr> write_seen;
        TVec<MemWrite> writes;
        collect_writes_to_alloca(alloca_val, write_seen, writes);

        if (writes.empty()) {
          continue;
        }

        TVec<Range> write_ranges;
        write_ranges.reserve(writes.size());
        for (auto &w : writes) {
          u64 sz = w.static_size != 0
                       ? w.static_size
                       : (w.size.is_constant_int()
                              ? static_cast<u64>(w.size.as_constant()->as_int())
                              : ~0ULL);
          write_ranges.push_back(get_range(w.ptr, alloca_val, sz));
        }

        struct BlockState {
          TVec<Range> covered;
          bool visited = false;
        };
        std::unordered_map<fir::BasicBlock, BlockState> block_in;

        TSet<fir::Instr> dead_candidates;
        for (auto &w : writes) {
          dead_candidates.insert(w.instr);
        }

        auto intersect_ranges = [](TVec<Range> &a, const TVec<Range> &b) {
          TVec<Range> result;
          for (auto &ra : a) {
            for (const auto &rb : b) {
              if (rb.covers(ra)) {
                result.push_back(ra);
                break;
              }
            }
          }
          a = std::move(result);
        };

        std::function<TVec<Range>(fir::BasicBlock)> walk_block_backward =
            [&](fir::BasicBlock b) -> TVec<Range> {
          auto &state = block_in[b];
          if (state.visited) {
            return state.covered;
          }
          state.visited = true;

          TVec<Range> covered;
          bool first = true;

          auto term = b->get_terminator();
          for (auto &edge : term->bbs) {
            auto succ_covered = walk_block_backward(edge.bb);
            if (first) {
              covered = succ_covered;
              first = false;
            } else {
              intersect_ranges(covered, succ_covered);
            }
          }
          if (term->bbs.empty()) {
            covered.clear();
            covered.push_back(Range{.whole_object = true});
          }

          auto &instrs = b->instructions;
          for (size_t i = instrs.size(); i-- > 0;) {
            fir::Instr cur = instrs[i];

            // Use AliasAnalysis to safely check if reads intersect with our
            // alloca
            if (cur->is(fir::InstrType::LoadInstr)) {
              u64 load_size = cur->get_type()->get_size();

              if (reads_alloca(cur->args[0]) ||
                  aa.alias(cur->args[0], alloca_val, load_size, 0) !=
                      AliasAnalyis::AAResult::NoAlias) {
                Range rr = get_range(cur->args[0], alloca_val, load_size);
                TVec<Range> kept;
                for (auto &c : covered) {
                  if (!c.overlaps(rr)) {
                    kept.push_back(c);
                  }
                }
                covered = std::move(kept);
              }
            } else if (cur->is(fir::InstrType::CallInstr) &&
                       cur->args[0].is_constant_func() &&
                       cur->args[0].as_constant()->as_func()->name ==
                           "foptim.memcpy") {
              fir::ValueR src = cur->args[2];
              u64 load_size =
                  cur->args[3].is_constant_int()
                      ? static_cast<u64>(cur->args[3].as_constant()->as_int())
                      : ~0ULL;

              if (reads_alloca(src) ||
                  aa.alias(src, alloca_val, load_size == ~0ULL ? 0 : load_size,
                           0) != AliasAnalyis::AAResult::NoAlias) {
                Range rr = get_range(src, alloca_val, load_size);
                TVec<Range> kept;
                for (auto &c : covered) {
                  if (!c.overlaps(rr)) {
                    kept.push_back(c);
                  }
                }
                covered = std::move(kept);
              }
            }

            for (size_t wi = 0; wi < writes.size(); ++wi) {
              if (writes[wi].instr != cur) {
                continue;
              }
              bool covered_here = false;
              for (auto &c : covered) {
                if (c.covers(write_ranges[wi])) {
                  covered_here = true;
                  break;
                }
              }
              if (!covered_here) {
                dead_candidates.erase(cur);
              }
              if (!write_ranges[wi].unknown) {
                covered.push_back(write_ranges[wi]);
              } else {
                covered.clear();
              }
              break;
            }
          }

          state.covered = covered;
          return covered;
        };

        for (auto &b2 : func.basic_blocks) {
          auto term = b2->get_terminator();
          if (term->bbs.empty()) {
            walk_block_backward(b2);
          }
        }

        // FIX: Evict any writes belonging to unreachable/infinite-loop blocks
        // that were never evaluated in the backward walk.
        TVec<fir::Instr> unvisited_writes;
        for (auto i : dead_candidates) {
          if (!block_in[i->get_parent()].visited) {
            unvisited_writes.push_back(i);
          }
        }
        for (auto i : unvisited_writes) {
          dead_candidates.erase(i);
        }

        for (auto &w : writes) {
          if (dead_candidates.contains(w.instr)) {
            w.instr.destroy();
            changed = true;
          }
        }
      }
    }

    return changed;
  }

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("DSE", COLOR_OPTIMF);
    CFG &cfg = AnalysisManager::cfg(func);
    Dominators &dom = AnalysisManager::dom(func);
    AliasAnalyis aa;

    eliminate_dead_alloca_stores(ctx, func, dom, cfg, aa);
    return PreservedAnalysis::cfg_only();
  }
};

} // namespace foptim::optim
