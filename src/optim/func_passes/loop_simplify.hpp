#pragma once
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
#include "utils/bitset.hpp"
#include <algorithm>

namespace foptim::optim {

class LoopSimplify final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    (void)ctx;
    CFG cfg{func};
    Dominators dom{cfg};
    LoopInfoAnalysis loops{dom};

    size_t loop_ip1 = 1;
    // eliminate all loops that dont have sideffects
    //  TODO: handle if its values are used after and their result value is
    //  known/can be calculated withuot loop
    for (; loop_ip1 <= loops.info.size(); loop_ip1++) {
      auto &loop = loops.info.at(loop_ip1 - 1);
      bool has_sideeffects = false;
      TVec<fir::Use> use_after;
      for (auto b : loop.body_nodes) {
        if (has_sideeffects) {
          break;
        }
        for (auto arg : cfg.bbrs[b].bb->args) {
          for (auto u : arg->uses) {
            auto use_bb = cfg.get_bb_id(u.user->get_parent());
            if (u.type == fir::UseType::BBArg) {
              if (!std::ranges::contains(
                      loop.body_nodes,
                      cfg.get_bb_id(u.user->bbs[u.argId].bb))) {
                use_after.push_back(u);
              }
            } else if (!std::ranges::contains(loop.body_nodes, use_bb)) {
              use_after.push_back(u);
            }
          }
        }
        for (auto i : cfg.bbrs[b].bb->instructions) {
          if (i->has_pot_sideeffects()) {
            has_sideeffects = true;
            break;
          }
          for (auto u : i->uses) {
            auto use_bb = cfg.get_bb_id(u.user->get_parent());
            if (u.type == fir::UseType::BBArg) {
              if (!std::ranges::contains(
                      loop.body_nodes,
                      cfg.get_bb_id(u.user->bbs[u.argId].bb))) {
                use_after.push_back(u);
              }
            } else if (!std::ranges::contains(loop.body_nodes, use_bb)) {
              use_after.push_back(u);
            }
          }
        }
      }
      if (!has_sideeffects && use_after.empty()) {
        auto head_term = cfg.bbrs[loop.head].bb->get_terminator();
        ASSERT(head_term->is(fir::InstrType::CondBranchInstr))
        fir::Builder bb{head_term};
        auto leave_index = 0;
        if (std::ranges::contains(loop.body_nodes,
                                  cfg.get_bb_id(head_term->bbs[0].bb))) {
          leave_index = 1;
        }
        auto new_branch = bb.build_branch(head_term->bbs[leave_index].bb);
        for (auto a : head_term->bbs[leave_index].args) {
          new_branch.add_bb_arg(0, a);
        }
        fmt::println("{} {}", head_term, leave_index);
        head_term.destroy();
        fmt::println("{}", func);
        cfg.update(func, false);
        dom.update(cfg);
        loops.update(dom);
        loop_ip1 = 0;
        continue;
      }
    }
  }
};
} // namespace foptim::optim
