
#pragma once
#include <fmt/base.h>

#include <deque>

#include "../function_pass.hpp"
#include "ir/builder.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"

namespace foptim::optim {

class LICM final : public FunctionPass {
public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("LICM");
    CFG &cfg = AnalysisManager::cfg(func);
    Dominators &dom = AnalysisManager::dom(func);
    LoopInfoAnalysis linfo{dom};

    bool modified = false;
    for (auto &info : linfo.info) {
      modified |= apply(ctx, func, cfg, info);
    }
    // instructions get moved, blocks and edges stay
    return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
  }

  // a load of a constant offset into a global that stays in the global's bounds
  // can be executed speculatively
  static bool is_dereferenceable(fir::ValueR ptr, u64 size) {
    i128 offset = 0;
    while (ptr.is_instr() && ptr.as_instr()->is(fir::BinaryInstrSubType::PtrAdd)) {
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
    for (auto instr : candidates) {
      const auto t = static_cast<ST>(instr->subtype);
      if (!is_assoc(t) && t != ST::IntSub) {
        continue;
      }
      if (!instr->get_type()->is_int()) {
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
      } else if (t != ST::IntSub && defined_outside(cfg, info, instr->args[0]) &&
                 !defined_outside(cfg, info, instr->args[1])) {
        jv = instr->args[1];
        c = instr->args[0];
        j_is_lhs = false;
      } else {
        continue;
      }
      if (!jv.is_instr() || jv.as_instr()->get_n_uses() != 1 ||
          !jv.as_instr()->is(fir::InstrType::BinaryInstr) ||
          jv.as_instr()->get_type() != instr->get_type()) {
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
      instr.destroy();
      j.destroy();
      modified = true;
    }
    return modified;
  }

  // returns whether anything was hoisted
  bool apply(fir::Context & /*unused*/, fir::Function &func, CFG &cfg,
             LoopInfo &info) {
    (void)func;

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

    const bool reassociated = reassociate(cfg, info, pre_header);

    std::deque<fir::Instr, utils::TempAlloc<fir::Instr>> worklist;
    TVec<fir::ValueR> invariant;

    // loads can only be hoisted if nothing in the loop writes memory
    bool loop_writes_mem = false;
    for (auto bb : info.body_nodes) {
      for (auto instr : cfg.bbrs[bb].bb->instructions) {
        loop_writes_mem |= instr->pot_modifies_mem();
      }
    }

    for (auto bb : info.body_nodes) {
      for (auto instr : cfg.bbrs[bb].bb->instructions) {
        if (instr->pot_modifies_mem() || instr->has_pot_sideeffects() ||
            instr->is_critical()) {
          continue;
        }
        // other reads (memcmp) are not speculated, they can fault
        if (instr->pot_reads_mem() && !instr->is(fir::InstrType::LoadInstr)) {
          continue;
        }
        if (instr->is(fir::InstrType::LoadInstr)) {
          if (loop_writes_mem || instr->Volatile || instr->Atomic ||
              !is_dereferenceable(instr->get_arg(0),
                                  instr->get_type()->get_size())) {
            continue;
          }
        }
        worklist.push_back(instr);
      }
    }

    while (!worklist.empty()) {
      auto instr = worklist.front();
      worklist.pop_front();
      if (instr->pot_modifies_mem() || instr->has_pot_sideeffects() ||
          instr->is_critical() ||
          (instr->pot_reads_mem() && !instr->is(fir::InstrType::LoadInstr))) {
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
        invariant.emplace_back(instr);
        // TODO: push uses into worklist but only if they are still in teh loop
      }
    }
    if (invariant.empty()) {
      return reassociated;
    }

    fir::Builder bb{pre_header};
    bb.at_penultimate(pre_header);
    for (auto inv : invariant) {
      auto old = inv.as_instr();
      auto copy = bb.insert_copy(old);
      old->replace_all_uses(fir::ValueR{copy});
    }

    for (auto inv : invariant) {
      inv.as_instr().destroy();
    }
    return true;
  }
};

} // namespace foptim::optim
