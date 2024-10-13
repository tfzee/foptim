#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

// cleans out merges blocks and cleans cfg in general
class Clean final : public FunctionPass {
public:
  void apply(fir::Context & /*unused*/, fir::Function &func) override {
    CFG cfg{func};

    cfg.postorder([&cfg](CFG::Node n) {
      using enum CFG::IterRes;
      // TODO: merge conditional branches to same target into simple jump to
      // that target
      auto term = n.bb->get_terminator();

      if (term.is_valid() && term->is(fir::InstrType::BranchInstr)) {
        CFG::Node &succ = cfg.bbrs[n.succ[0]];
        assert(n.succ.size() == 1);


        if (n.bb->n_instrs() == 1 && n.bb->n_args() == 0) {
          // if we got no bb args and were empty we can replace all incoming
          // edges with our outgoing edge

          FVec<fir::ValueR>& old_args = term->bbs[0].args;
          for (auto pred : n.pred) {
            auto pred_bb = cfg.bbrs[pred].bb;
            auto pred_term = pred_bb->get_terminator();

            pred_term.replace_bb(n.bb, succ.bb);

            if(succ.bb->n_args() != 0){
              for(auto & old_arg : old_args){
                pred_term.add_bb_arg(succ.bb, old_arg);
              }
            }
          }
          ASSERT(n.bb->get_n_uses() == 0);
          n.bb->remove_from_parent(true);
          return Changed;
        }

        if (succ.bb->n_args() == 0) {
          //  if the successor has no args we  can copy our bb_args ontop of the
          //     successor and then forward all
          //     incoming edges to the succ
          if (n.bb->n_instrs() == 1) {
            for (auto pred : n.pred) {
              auto pred_bb = cfg.bbrs[pred].bb;
              pred_bb->get_terminator().replace_bb(n.bb, succ.bb);
            }

            // then we need to move all uses of our own bbargs to the new args
            succ.bb->args = n.bb->args;
            for (u32 arg_id = 0; arg_id < succ.bb->args.size(); arg_id++) {
              succ.bb->args[arg_id].uses.clear();

              n.bb->args[arg_id].replace_all_uses(fir::ValueR{succ.bb, arg_id});
              // utils::Debug << "=========" << old_uses
              //              << "  == " << succ.bb->args[arg_id].get_n_uses()
              //              << "\n";
              // utils::Debug << "=========" << fir::ValueR(succ.bb, arg_id) <<
              // "\n"; ASSERT(succ.bb->args[arg_id].get_n_uses() == old_uses);
            }
            if(n.bb->get_n_uses() != 0){
              utils::Debug << n.bb->get_parent();
              utils::Debug << n.bb;
              for(auto use: n.bb->get_uses()){
                utils::Debug << "USE " << use << "\n";
              }
            }
            ASSERT(n.bb->get_n_uses() == 0);
            n.bb->remove_from_parent(true);
            return Changed;
          }

          // if succ empty
          if (succ.bb->n_instrs() == 1) {
            // replace this terminator with succ terminator
            n.bb->set_terminator(succ.bb->get_terminator());
            return None;
          }
        }
      }
      return None;
    });
  }
};

} // namespace foptim::optim
