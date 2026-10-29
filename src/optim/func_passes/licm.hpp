
#pragma once
#include <fmt/base.h>

#include <deque>

#include "../function_pass.hpp"
#include "ir/builder.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/memory_ssa.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim {

class LICM final : public FunctionPass {
  bool split_edges = false;

 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("LICM");
    CFG &cfg = AnalysisManager::cfg(func);
    Dominators &dom = AnalysisManager::dom(func);
    LoopInfoAnalysis linfo{dom};

    bool modified = false;
    split_edges = false;
    for (auto &info : linfo.info) {
      modified |= apply(ctx, func, cfg, info);
      if (split_edges) {
        // the cfg is stale now
        modified = true;
        break;
      }
    }
    if (split_edges) {
      return PreservedAnalysis::none();
    }
    // instructions get moved, blocks and edges stay
    return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
  }

  // a load of a constant offset into a global that stays in the global's bounds
  // can be executed speculatively
  static bool is_dereferenceable(fir::ValueR ptr, u64 size) {
    i128 offset = 0;
    while (ptr.is_instr() &&
           ptr.as_instr()->is(fir::BinaryInstrSubType::PtrAdd)) {
      auto off = ptr.as_instr()->get_arg(1);
      if (!off.is_constant() || !off.as_constant()->is_int()) {
        return false;
      }
      offset += off.as_constant()->as_int();
      ptr = ptr.as_instr()->get_arg(0);
    }
    if (!ptr.is_constant() || !ptr.as_constant()->is_global()) {
      return false;
    }
    auto glob = ptr.as_constant()->as_global();
    return !glob->is_extern_decl() && offset >= 0 &&
           offset + size <= glob->n_bytes;
  }

  // Does the load see the memory the loop was entered with? True if the nearest
  // access that may write what it reads is outside of the loop, nothing in the
  // loop changes the value then.
  static bool sees_memory_of_entry(fir::Function &func, const LoopInfo &info,
                                   fir::Instr load) {
    auto &mssa = AnalysisManager::memory_ssa(func);
    if (mssa.access_of(load) == MemorySSA::None) {
      return false;
    }
    AliasQuery aq{AnalysisManager::provenance(func)};
    MemorySSAWalker walker{mssa, AnalysisManager::mem_accesses(func), aq};
    const u32 c = walker.clobber(load);
    return c == MemorySSA::LiveOnEntry ||
           std::ranges::find(info.body_nodes, mssa[c].block) ==
               info.body_nodes.end();
  }

  // defined outside of the loop or a constant
  static bool defined_outside(const CFG &cfg, const LoopInfo &info,
                              fir::ValueR v) {
    fir::BasicBlock vbb{fir::BasicBlock::invalid()};
    if (v.is_instr()) {
      vbb = v.as_instr()->get_parent();
    } else if (v.is_bb_arg()) {
      vbb = v.as_bb_arg()->get_parent();
    } else {
      return true;
    }
    return std::ranges::find(info.body_nodes, cfg.get_bb_id(vbb)) ==
           info.body_nodes.end();
  }

  // (x op c1) op c2 with invariant c1 and c2 becomes x op (c1 op c2) so the
  // inner operation can leave the loop. Returns whether anything changed.
  bool reassociate(const CFG &cfg, const LoopInfo &info,
                   fir::BasicBlock pre_header) {
    using ST = fir::BinaryInstrSubType;
    auto is_assoc = [](ST t) {
      return t == ST::IntAdd || t == ST::IntMul || t == ST::And ||
             t == ST::Or || t == ST::Xor;
    };
    bool modified = false;
    TVec<fir::Instr> candidates;
    for (auto bb : info.body_nodes) {
      for (auto instr : cfg.bbrs[bb].bb->instructions) {
        if (instr->is(fir::InstrType::BinaryInstr)) {
          candidates.push_back(instr);
        }
      }
    }
    TSet<fir::Instr> destroyed;
    for (auto instr : candidates) {
      if (destroyed.contains(instr)) {
        continue;
      }
      const auto t = static_cast<ST>(instr->subtype);
      if (!is_assoc(t) && t != ST::IntSub) {
        continue;
      }
      if (!instr->result_type()->is_int()) {
        continue;
      }
      // outer operands: j is the inner instruction, c the invariant side
      fir::ValueR c;
      fir::ValueR jv;
      bool j_is_lhs = true;
      if (defined_outside(cfg, info, instr->args[1]) &&
          !defined_outside(cfg, info, instr->args[0])) {
        jv = instr->args[0];
        c = instr->args[1];
      } else if (t != ST::IntSub &&
                 defined_outside(cfg, info, instr->args[0]) &&
                 !defined_outside(cfg, info, instr->args[1])) {
        jv = instr->args[1];
        c = instr->args[0];
        j_is_lhs = false;
      } else {
        continue;
      }
      if (!jv.is_instr() || jv.as_instr()->get_n_uses() != 1 ||
          !jv.as_instr()->is(fir::InstrType::BinaryInstr) ||
          jv.as_instr()->result_type() != instr->result_type()) {
        continue;
      }
      auto j = jv.as_instr();
      const auto jt = static_cast<ST>(j->subtype);
      if (!is_assoc(jt) && jt != ST::IntSub) {
        continue;
      }
      // k is the variant operand of j, d the invariant one
      fir::ValueR k;
      fir::ValueR d;
      bool d_is_rhs = true;
      if (defined_outside(cfg, info, j->args[1]) &&
          !defined_outside(cfg, info, j->args[0])) {
        k = j->args[0];
        d = j->args[1];
      } else if (jt != ST::IntSub && defined_outside(cfg, info, j->args[0]) &&
                 !defined_outside(cfg, info, j->args[1])) {
        k = j->args[1];
        d = j->args[0];
      } else {
        continue;
      }
      (void)d_is_rhs;
      // result = k (+|-) merged, merged is built from d and c
      ST outer;
      ST merge;
      fir::ValueR ma;
      fir::ValueR mb;
      if (t == jt && t != ST::IntSub) {
        outer = t;
        merge = t;
        ma = d;
        mb = c;
      } else if (t == ST::IntSub && jt == ST::IntSub) {
        // (k - d) - c = k - (d + c)
        outer = ST::IntSub;
        merge = ST::IntAdd;
        ma = d;
        mb = c;
      } else if (t == ST::IntAdd && jt == ST::IntSub && j_is_lhs == false) {
        // c + (k - d) = k + (c - d)
        outer = ST::IntAdd;
        merge = ST::IntSub;
        ma = c;
        mb = d;
      } else if (t == ST::IntAdd && jt == ST::IntSub) {
        outer = ST::IntAdd;
        merge = ST::IntSub;
        ma = c;
        mb = d;
      } else if (t == ST::IntSub && jt == ST::IntAdd) {
        // (k + d) - c = k + (d - c)
        outer = ST::IntAdd;
        merge = ST::IntSub;
        ma = d;
        mb = c;
      } else {
        continue;
      }
      // j = d - k (invariant minus variant) is not handled above
      if (jt == ST::IntSub && j->args[0] != k) {
        continue;
      }
      fir::Builder pb{pre_header};
      pb.at_penultimate(pre_header);
      auto merged = pb.build_binary_op(ma, mb, merge);
      fir::Builder ib{instr};
      auto res = ib.build_binary_op(k, merged, outer);
      instr->replace_all_uses(res);
      destroyed.insert(instr);
      destroyed.insert(j);
      instr.destroy();
      j.destroy();
      modified = true;
    }
    return modified;
  }

  // the loop is entered through an unconditional edge from the preheader and
  // the compare of the header (a header argument that starts as a constant
  // against a constant) sends the first iteration into the loop body, so
  // everything that runs on each iteration runs at least once
  static bool body_runs_on_entry(const CFG &cfg, const LoopInfo &info,
                                 fir::BasicBlock pre_header) {
    auto header = cfg.bbrs[info.head].bb;
    auto term = header->get_terminator();
    auto pre_term = pre_header->get_terminator();
    size_t edge = 0;
    size_t edges = 0;
    for (size_t i = 0; i < pre_term->bbs.size(); i++) {
      if (pre_term->bbs[i].bb == header) {
        edge = i;
        edges++;
      }
    }
    if (!term->is(fir::InstrType::CondBranchInstr) || edges != 1 ||
        info.leaving_nodes.size() != 1 || info.leaving_nodes[0] != info.head) {
      return false;
    }
    auto cond = term->get_arg(0);
    if (!cond.is_instr() || !cond.as_instr()->is(fir::InstrType::ICmp) ||
        cond.as_instr()->get_parent() != header) {
      return false;
    }
    auto cmp = cond.as_instr();
    auto start = [&](fir::ValueR v) -> std::optional<i128> {
      if (v.is_constant() && v.as_constant()->is_int()) {
        return v.as_constant()->as_int();
      }
      if (v.is_bb_arg()) {
        for (size_t i = 0; i < header->args.size(); i++) {
          if (header->args[i] != v.as_bb_arg()) {
            continue;
          }
          auto inc = pre_term->bbs[edge].args[i];
          if (inc.is_constant() && inc.as_constant()->is_int()) {
            return inc.as_constant()->as_int();
          }
        }
      }
      return std::nullopt;
    };
    auto a = start(cmp->get_arg(0));
    auto b = start(cmp->get_arg(1));
    if (!a || !b) {
      return false;
    }
    bool taken;
    using ST = fir::ICmpInstrSubType;
    if (cmp->is(ST::SLT)) {
      taken = *a < *b;
    } else if (cmp->is(ST::SLE)) {
      taken = *a <= *b;
    } else if (cmp->is(ST::SGT)) {
      taken = *a > *b;
    } else if (cmp->is(ST::SGE)) {
      taken = *a >= *b;
    } else if (cmp->is(ST::EQ)) {
      taken = *a == *b;
    } else if (cmp->is(ST::NE)) {
      taken = *a != *b;
    } else {
      return false;
    }
    // true target index 0, false target index 1
    auto inside = [&](size_t i) {
      return std::ranges::find(info.body_nodes,
                               cfg.get_bb_id(term->bbs[i].bb)) !=
             info.body_nodes.end();
    };
    return inside(taken ? 0 : 1);
  }

  // returns whether anything was hoisted
  bool apply(fir::Context & /*unused*/, fir::Function &func, CFG &cfg,
             LoopInfo &info) {
    if (cfg.bbrs[info.head].pred.size() != info.tails.size() + 1) {
      failure({.reason = "TODO: Cant apply licm without a preheader\n",
               .loc = cfg.bbrs[info.head].bb});
      return false;
    }

    fir::BasicBlock pre_header{fir::BasicBlock{fir::BasicBlock::invalid()}};
    for (auto i : cfg.bbrs[info.head].pred) {
      if (std::ranges::find(info.body_nodes, i) == info.body_nodes.end()) {
        ASSERT(!pre_header.is_valid());
        pre_header = cfg.bbrs[i].bb;
        // TODO: break
      }
    }
    if (!pre_header.is_valid()) {
      info.dump();
      // print << cfg.bbrs[info.head].pred.size() << "\n";
      // print << cfg.bbrs[info.head].bb->get_parent() << "\n";
      // print << cfg.bbrs[info.head].pred << "\n";
      ASSERT(false);
      failure({.reason = "TODO: Cant apply licm without a preheader\n",
               .loc = cfg.bbrs[info.head].bb});
      return false;
    }

    // a division that runs on every entry of the loop still must not move in
    // front of a guard, so the entry edge gets its own block
    bool split_edge = false;
    if (pre_header->get_terminator()->is(fir::InstrType::CondBranchInstr) &&
        info.body_nodes.size() == 2 &&
        body_runs_on_entry(cfg, info, pre_header)) {
      bool has_div = false;
      for (auto bb : info.body_nodes) {
        for (auto instr : cfg.bbrs[bb].bb->instructions) {
          has_div |= instr->is(fir::BinaryInstrSubType::IntSDiv) ||
                     instr->is(fir::BinaryInstrSubType::IntUDiv) ||
                     instr->is(fir::BinaryInstrSubType::IntSRem) ||
                     instr->is(fir::BinaryInstrSubType::IntURem);
        }
      }
      if (has_div) {
        auto header = cfg.bbrs[info.head].bb;
        auto pre_term = pre_header->get_terminator();
        u16 edge = pre_term.get_bb_id(header);
        TVec<fir::ValueR> edge_args{pre_term->bbs[edge].args.begin(),
                                    pre_term->bbs[edge].args.end()};
        fir::Builder b{pre_header};
        auto entry = b.append_bb();
        b.at_end(entry);
        auto br = b.build_branch(header);
        for (auto arg : edge_args) {
          br.add_bb_arg(0, arg);
        }
        pre_term.replace_bb(edge, entry, false);
        pre_header = entry;
        split_edge = true;
      }
    }
    split_edges |= split_edge;

    const bool reassociated = reassociate(cfg, info, pre_header);
    if (reassociated) {
      AnalysisManager::drop_memory(func);
    }

    std::deque<fir::Instr, utils::TempAlloc<fir::Instr>> worklist;
    TVec<fir::ValueR> invariant;

    // a memcmp can only be hoisted if nothing in the loop writes memory
    bool loop_writes_mem = false;
    for (auto bb : info.body_nodes) {
      for (auto instr : cfg.bbrs[bb].bb->instructions) {
        loop_writes_mem |= instr->pot_modifies_mem();
      }
    }

    // a direct call to a function that only reads memory, it can leave the
    // loop like a load when nothing in the loop changes what it reads
    auto is_readonly_call = [](fir::Instr instr) {
      if (!instr->is(fir::InstrType::CallInstr) || instr->n_results() != 1) {
        return false;
      }
      auto callee = instr->get_arg(0);
      if (!callee.is_constant() || !callee.as_constant()->is_func()) {
        return false;
      }
      const auto &attribs = callee.as_constant()->as_func()->attribs;
      return attribs.mem_read_only && !attribs.mem_read_none;
    };

    // Reads can fault, so they only leave the loop if they run on every entry
    // anyway: in the header, behind an unconditional edge from the
    // preheader, with nothing before them that has an effect.
    TSet<fir::Instr> runs_on_entry;
    if (pre_header->get_terminator()->is(fir::InstrType::BranchInstr)) {
      for (auto instr : cfg.bbrs[info.head].bb->instructions) {
        if (is_readonly_call(instr)) {
          runs_on_entry.insert(instr);
          // it may not return, then nothing behind it runs on entry
          if (!instr->get_arg(0)
                   .as_constant()
                   ->as_func()
                   ->attribs.must_return) {
            break;
          }
          continue;
        }
        if (instr->has_pot_sideeffects() || instr->is_critical()) {
          break;
        }
        runs_on_entry.insert(instr);
      }
    }
    // division traps on a zero divisor (and on INT_MIN / -1), so it only
    // leaves the loop if the divisor is a constant that rules both out or if
    // it runs on every entry anyway
    // with a header and a single body block, the body is the only way on
    const bool body_entered = info.body_nodes.size() == 2 &&
                              body_runs_on_entry(cfg, info, pre_header);
    auto may_trap = [&](fir::Instr instr) {
      using ST = fir::BinaryInstrSubType;
      if (!instr->is(ST::IntSDiv) && !instr->is(ST::IntUDiv) &&
          !instr->is(ST::IntSRem) && !instr->is(ST::IntURem)) {
        return false;
      }
      if (runs_on_entry.contains(instr)) {
        return false;
      }
      if (body_entered) {
        return false;
      }
      auto d = instr->get_arg(1);
      if (!d.is_constant() || !d.as_constant()->is_int()) {
        return true;
      }
      auto v = d.as_constant()->as_int();
      return v == 0 || v == -1;
    };
    TMap<fir::Instr, bool> unchanged_in_loop;
    auto is_hoistable_read = [&](fir::Instr instr) {
      if (instr->Volatile || instr->Atomic) {
        return false;
      }
      if (instr->is(fir::InstrType::LoadInstr)) {
        auto [it, fresh] = unchanged_in_loop.try_emplace(instr, false);
        if (fresh) {
          it->second = sees_memory_of_entry(func, info, instr);
        }
        if (!it->second) {
          return false;
        }
        if (is_dereferenceable(instr->get_arg(0),
                               instr->result_type()->get_size())) {
          return true;
        }
      } else if (is_readonly_call(instr)) {
        auto [it, fresh] = unchanged_in_loop.try_emplace(instr, false);
        if (fresh) {
          it->second = sees_memory_of_entry(func, info, instr);
        }
        if (!it->second) {
          return false;
        }
      } else if (loop_writes_mem) {
        return false;
      }
      return runs_on_entry.contains(instr);
    };

    for (auto bb : info.body_nodes) {
      for (auto instr : cfg.bbrs[bb].bb->instructions) {
        const bool ro_call = is_readonly_call(instr);
        if (!ro_call &&
            (instr->pot_modifies_mem() || instr->has_pot_sideeffects() ||
             instr->is_critical())) {
          continue;
        }
        if (ro_call || instr->pot_reads_mem()) {
          const bool is_load = instr->is(fir::InstrType::LoadInstr);
          const bool is_memcmp = instr->is(fir::IntrinsicSubType::Memcmp);
          if ((!is_load && !is_memcmp && !ro_call) ||
              !is_hoistable_read(instr)) {
            continue;
          }
        }
        if (is_cheap_to_rematerialize(instr) || may_trap(instr)) {
          continue;
        }
        worklist.push_back(instr);
      }
    }

    while (!worklist.empty()) {
      auto instr = worklist.front();
      worklist.pop_front();
      const bool ro_call = is_readonly_call(instr);
      if ((!ro_call &&
           (instr->pot_modifies_mem() || instr->has_pot_sideeffects() ||
            instr->is_critical())) ||
          ((ro_call || instr->pot_reads_mem()) && !is_hoistable_read(instr)) ||
          may_trap(instr)) {
        continue;
      }

      bool all_args_are_invariant = true;
      for (auto arg : instr->args) {
        fir::BasicBlock arg_bb{fir::BasicBlock{fir::BasicBlock::invalid()}};
        if (arg.is_instr()) {
          arg_bb = arg.as_instr()->get_parent();
        } else if (arg.is_bb_arg()) {
          arg_bb = arg.as_bb_arg()->get_parent();
        } else if (arg.is_constant()) {
          continue;
        } else {
          UNREACH();
        }
        auto arg_bb_id = cfg.get_bb_id(arg_bb);

        if (std::ranges::find(invariant, arg) == invariant.end() &&
            std::ranges::find(info.body_nodes, arg_bb_id) !=
                info.body_nodes.end()) {
          all_args_are_invariant = false;
          break;
        }
      }
      if (all_args_are_invariant) {
        invariant.push_back(instr.result());
        // TODO: push uses into worklist but only if they are still in teh loop
      }
    }
    if (invariant.empty()) {
      return reassociated || split_edge;
    }

    fir::Builder bb{pre_header};
    bb.at_penultimate(pre_header);
    for (auto inv : invariant) {
      auto old = inv.as_instr();
      auto copy = bb.insert_copy(old);
      old->replace_all_uses(copy.result());
    }

    for (auto inv : invariant) {
      inv.as_instr().destroy();
    }
    // the hoisted instructions are new ones, and the cached accesses name the
    // values they replace
    AnalysisManager::drop_memory(func);
    return true;
  }
};

}  // namespace foptim::optim
