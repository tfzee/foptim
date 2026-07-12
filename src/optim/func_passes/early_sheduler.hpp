#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "utils/todo.hpp"
#include <fmt/base.h>
#include <optional>

namespace foptim::optim {

/*
Try to move instructions to later position to shorten livespans
*/
class EarlySheduler final : public FunctionPass {
public:
  struct Config {};
  Config config;

  bool is_alive(fir::ValueR i, CFG &cfg, DominatorTree &dom, u32 target_bb_id) {
    for (const auto &u : *i.get_uses()) {
      auto use_bb = u.user->get_parent();
      auto use_bb_id = cfg.get_bb_id(use_bb);
      // if our target_bb dominates our use of this argument then it must? be
      // alive till then
      if (dom.strict_dominates(target_bb_id, use_bb_id)) {
        return true;
      }
    }
    return false;
  }

  void move_to_latest_position(fir::Instr instr, CFG &cfg, DominatorTree &dom,
                               LoopInfoAnalysis &loop_info) {
    // find bb that dominates all uses
    //  if its not the origin bb then it *could* be moved
    //  however that extends the lifetime of all of its args(if its not a
    //  constant) so gotta check if worth
    auto origin_bb = instr->get_parent();
    auto origin_bb_id = cfg.get_bb_id(instr->get_parent());

    std::optional<u32> opt_curr_bb_id_target = std::nullopt;
    for (auto use : instr->get_uses()) {
      // if we have use in origin bb  we can't move it
      if (use.user->get_parent() == origin_bb) {
        return;
      }
      auto use_bb_id = cfg.get_bb_id(use.user->get_parent());
      if (!opt_curr_bb_id_target.has_value()) {
        opt_curr_bb_id_target = use_bb_id;
      }
      opt_curr_bb_id_target =
          dom.common_denom(use_bb_id, opt_curr_bb_id_target.value());
      if (opt_curr_bb_id_target.value() == origin_bb_id) {
        return;
      }
    }
    if (!opt_curr_bb_id_target.has_value() ||
        opt_curr_bb_id_target.value() == origin_bb_id) {
      return;
    }
    auto curr_bb_id_target = opt_curr_bb_id_target.value();
    ASSERT(dom.dominates(origin_bb_id, curr_bb_id_target));

    // check that we dont move into loop unless were part of that loop
    for (auto loop : loop_info.info) {
      bool target_is_in_loop =
          std::ranges::find(loop.body_nodes, curr_bb_id_target) !=
          loop.body_nodes.end();
      if (target_is_in_loop) {
        bool origin_in_same_loop =
            std::ranges::find(loop.body_nodes, origin_bb_id) !=
            loop.body_nodes.end();
        if (!origin_in_same_loop) {
          return;
        }
      }
    }

    // check that the arguments to the instruction we want to move are alive
    // anyways so moving doesnt extend their lifetimes
    for (auto arg : instr->args) {
      if (arg.is_constant()) {
        continue;
      }
      if (!is_alive(arg, cfg, dom, curr_bb_id_target)) {
        return;
      }
    }

    auto bb = cfg.bbrs[curr_bb_id_target].bb.builder();
    auto new_instr = bb.insert_copy(instr);
    instr->replace_all_uses(fir::ValueR{new_instr});
  }

  void apply(fir::Context & /*ctx*/, fir::Function &func) override {
    ZoneScopedNC("EarlySheduler", COLOR_OPTIMF);
    CFG cfg{func};
    DominatorTree dom{cfg};
    LoopInfoAnalysis loop{dom};

    for (auto bb : func.basic_blocks) {
      for (auto i : bb->instructions) {
        if (i->has_pot_sideeffects() || i->pot_reads_mem() ||
            i->pot_modifies_mem() || i->get_n_uses() == 0) {
          continue;
        }
        move_to_latest_position(i, cfg, dom, loop);
      }
    }
  }
};
} // namespace foptim::optim
