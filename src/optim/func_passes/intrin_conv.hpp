#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include <fmt/base.h>
#include <optional>

/*
Try to convert intrinsic like patterns into the actual intrinsics this includes
not proper intrinsics like memcpy/memset etc
*/

namespace foptim::optim {

namespace {

// the block args passed on the loop exit edge, empty optional if one is
// defined inside the loop (the loop is removed so it can not be forwarded)
std::optional<TVec<fir::ValueR>> exit_edge_args(fir::Instr term,
                                                fir::BasicBlock loop_bb,
                                                size_t exit_idx) {
  TVec<fir::ValueR> res;
  for (auto arg : term->get_bb_args()[exit_idx].args) {
    if ((arg.is_instr() && arg.as_instr()->get_parent() == loop_bb) ||
        (arg.is_bb_arg() && arg.as_bb_arg()->get_parent() == loop_bb)) {
      return std::nullopt;
    }
    res.push_back(arg);
  }
  return res;
}

// detect single basicblock memset/memcpy
bool match_memsetcpy(fir::Function &func) {
  /*
  something like
  loop(0x61136f0cda80{}: i64 NUSES: 2):
   0x61136f0e9b88: ptr = PtrAdd(0x61136f0c9e58, 0x61136f0cda80)
   0x61136f0e9c10: i8 = Store(0x61136f0e9b88, 0:i8){}
   0x61136f0e9eb8: i64 = IntAdd(0x61136f0cda80, 1:i64){}
   0x61136f0c9c38: i1 = IntULE(0x61136f0e9eb8, 15:i64){}
   0x61136f0c9b28: i1 = Not(0x61136f0c9c38){}
   0x61136f0c9bb0: () = CondBranch(0x61136f0c9b28){}
  */
  (void)func;
  CFG cfg{func};
  DominatorTree dom2{cfg};
  LoopInfoAnalysis linfo{dom2};
  LoopRangeAnalysis range{};

  if (linfo.info.empty()) {
    return false;
  }
  for (auto &l : linfo.info) {
    if (l.body_nodes.size() != 1 && l.body_nodes[0] == l.head) {
      continue;
    }
    if (!range.update(cfg, l)) {
      continue;
    }
    if (!range.known_lower || !range.known_upper) {
      continue;
    }
    auto r = range.upper_bound - range.lower_bound;
    auto loop_bb = cfg.bbrs[l.head].bb;

    // if all vlaues in this loop arent used afterwards && we dont load we only
    // have a store of a constant we can conver it
    bool outside_use = false;
    bool other_effects = false;
    fir::Instr store_instr{fir::Instr::invalid()};
    fir::Instr load_instr{fir::Instr::invalid()};
    for (auto i : loop_bb->instructions) {
      for (auto use : i->uses) {
        if (use.user->get_parent() != loop_bb) {
          outside_use = true;
          break;
        }
      }
      if (i->pot_modifies_mem() || i->pot_reads_mem() ||
          i->has_pot_sideeffects() || i->is_critical()) {
        if (i->is(fir::InstrType::StoreInstr) && !store_instr.is_valid()) {
          store_instr = i;
          continue;
        }
        if (i->is(fir::InstrType::LoadInstr) && !load_instr.is_valid()) {
          load_instr = i;
          continue;
        }
        if (i->is(fir::InstrType::CondBranchInstr) ||
            i->is(fir::InstrType::BranchInstr) ||
            i->is(fir::InstrType::SwitchInstr) ||
            i->is(fir::InstrType::ReturnInstr)) {
          continue;
        }
        other_effects = true;
        break;
      }
      if (outside_use) {
        break;
      }
    }

    if (other_effects || outside_use || !store_instr.is_valid()) {
      continue;
    }
    if (store_instr->Volatile || store_instr->Atomic) {
      continue;
    }

    // if we get load into store then we can do memcpy instead
    if (load_instr.is_valid()) {
      if (load_instr->get_type() != store_instr.get_type() ||
          store_instr->args[1] != fir::ValueR{load_instr} ||
          load_instr->Volatile || load_instr->Atomic) {
        continue;
      }
      size_t type_multiplier = load_instr->get_type()->get_size();
      fir::Builder buh{store_instr};
      auto old_term = loop_bb->get_terminator();
      ASSERT(old_term->bbs.size() == 2);
      const size_t exit_idx = old_term->bbs[0].bb == loop_bb ? 1 : 0;
      auto exit_args = exit_edge_args(old_term, loop_bb, exit_idx);
      if (!exit_args.has_value()) {
        continue;
      }
      buh.at_end(loop_bb);
      buh.build_intrinsic(
          store_instr->args[0], load_instr->args[0],
          fir::ValueR{func.ctx->get_constant_int(r * type_multiplier, 64)},
          fir::IntrinsicSubType::Memcpy, func.ctx->get_void_type());
      fir::BasicBlock target_bb{fir::BasicBlock::invalid()};
      if (old_term->bbs[0].bb == loop_bb) {
        target_bb = old_term->bbs[1].bb;
      } else if (old_term->bbs[1].bb == loop_bb) {
        target_bb = old_term->bbs[0].bb;
      }
      auto new_br = buh.build_branch(target_bb);
      for (auto arg : *exit_args) {
        new_br.add_bb_arg(0, arg);
      }
      load_instr.destroy();
      store_instr.destroy();
      old_term.destroy();
      return true;
    }
    // we need to store a constant
    auto store_val = store_instr->args[1];
    bool is_constant = store_val.is_constant();
    bool is_instr_loop_constant_value =
        store_val.is_instr() && loop_bb != store_val.as_instr()->get_parent();
    bool is_bb_arg_loop_constant_value =
        store_val.is_bb_arg() && loop_bb != store_val.as_bb_arg()->get_parent();
    if (!is_constant && !is_instr_loop_constant_value &&
        !is_bb_arg_loop_constant_value) {
      return false;
    }
    fir::Builder buh{store_instr};
    auto old_term = loop_bb->get_terminator();
    ASSERT(old_term->bbs.size() == 2);
    const size_t exit_idx = old_term->bbs[0].bb == loop_bb ? 1 : 0;
    auto exit_args = exit_edge_args(old_term, loop_bb, exit_idx);
    if (!exit_args.has_value()) {
      continue;
    }
    buh.at_end(loop_bb);
    buh.build_intrinsic(store_instr->args[0], store_instr->args[1],
                        fir::ValueR{func.ctx->get_constant_int(r, 64)},
                        fir::IntrinsicSubType::Memset,
                        func.ctx->get_void_type());
    fir::BasicBlock target_bb{fir::BasicBlock::invalid()};
    if (old_term->bbs[0].bb == loop_bb) {
      target_bb = old_term->bbs[1].bb;
    } else if (old_term->bbs[1].bb == loop_bb) {
      target_bb = old_term->bbs[0].bb;
    }
    auto new_br = buh.build_branch(target_bb);
    for (auto arg : *exit_args) {
      new_br.add_bb_arg(0, arg);
    }
    store_instr.destroy();
    old_term.destroy();
    return true;
  }
  return false;
}

}; // namespace

class IntrinConv final : public FunctionPass {
public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {

    (void)ctx;
    (void)func;
    ZoneScopedNC("IntrinConv", COLOR_OPTIMF);
    bool modified = match_memsetcpy(func);
    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};

} // namespace foptim::optim
