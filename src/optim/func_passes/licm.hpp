
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
          instr->is_critical()) {
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
      return false;
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
