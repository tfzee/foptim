#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "utils/logging.hpp"
#include "utils/vec.hpp"
#include <algorithm>

namespace foptim::optim {

class LoopRotate final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("LoopRotate");
    CFG cfg{func};
    Dominators dom{cfg};
    LoopInfoAnalysis linfo{dom};

    for (LoopInfo &loop : linfo.info) {
      if (apply(ctx, cfg, loop) && linfo.info.size() > 1) {
        cfg.update(func, false);
        dom.update(cfg);
        linfo.update(dom);
      }
    }
  }

  void replace_branch_with_header(TMap<fir::ValueR, fir::ValueR> &repl_map,
                                  uint8_t non_exiting_target,
                                  fir::BasicBlock header_bb,
                                  fir::BasicBlock head_pred) {
    // utils::Debug << header_bb->get_parent() << "\n";
    repl_map.clear();
    fir::Instr old_terminator = head_pred->get_terminator();
    auto &old_terminator_args = old_terminator->bbs[0].args;

    // head_pred->remove_instr();

    // we need the replacement for the bb args in case they are used in
    // the header
    {
      size_t arg_id = 0;
      for (auto arg : old_terminator_args) {
        repl_map.insert({fir::ValueR(header_bb, arg_id), arg});
        arg_id++;
      }
    }
    fir::Builder bb{head_pred};
    bb.at_end(head_pred);

    // IDK about this
    // since they are just inserted at the end we could just use the length
    //  FVec<fir::Instr> new_instrs;

    // utils::Debug << "doing subs\n";
    // utils::Debug << old_terminator_args << "\n";
    // utils::Debug << repl_map << "\n";
    for (auto instr : header_bb->instructions) {
      fir::Instr new_instr = bb.insert_copy(instr);
      new_instr.substitute(repl_map);
      repl_map.insert({fir::ValueR(instr), fir::ValueR(new_instr)});
    }

    // utils::Debug << header_bb->get_parent() << "\n";
    // utils::Debug << head_pred << "\n";
    // utils::Debug << header_bb << "\n";
    // clean up the arguments going into the loop based on the args of the old
    // terminator
    {
      fir::Instr new_term = head_pred->get_terminator();
      // new_term.clear_bb_args(non_exiting_target);
      // assert(new_term->bbs[non_exiting_target].args.size() == 0);

      new_term.replace_bb(non_exiting_target, header_bb, false);
      for (auto arg : old_terminator_args) {
        new_term.add_bb_arg(non_exiting_target, arg);
      }
    }

    old_terminator.remove_from_parent();
  }

  bool apply(fir::Context & /*unused*/, const CFG &cfg, LoopInfo &linfo) {
    // only if the header is the only exiting node
    if (linfo.leaving_nodes.size() == 0) {
      linfo.dump();
      failure({"No leaving node ", {cfg.bbrs[linfo.head].bb}});
      return false;
    }
    if (linfo.leaving_nodes.size() != 1 ||
        linfo.leaving_nodes[0] != linfo.head) {
      failure(
          {"Only leaving node can be the header ", {cfg.bbrs[linfo.head].bb}});
      return false;
    }
    auto header_bb = cfg.bbrs[linfo.head].bb;
    // only worth it if the header aint to big
    // TODO: magic number parameter
    if (header_bb->instructions.size() > 10) {
      failure({"Header to big ", {cfg.bbrs[linfo.head].bb}});
      return false;
    }
    // only if all of the operations in the header dont have sideeffects
    for (auto instr : header_bb->instructions) {
      if (instr->has_pot_sideeffects()) {
        failure({"Header has side effects ", {cfg.bbrs[linfo.head].bb}});
        return false;
      }
    }

    // if theres multiple incoming edges(from outside the loop) or one of the
    // incoming edges is conditional
    bool needs_preheader =
        cfg.bbrs[linfo.head].pred.size() > 1 + linfo.tails.size();
    if (!needs_preheader) {
      for (auto pred : cfg.bbrs[linfo.head].pred) {
        if (!cfg.bbrs[pred].bb->get_terminator()->is(
                fir::InstrType::BranchInstr)) {
          needs_preheader = true;
        }
      }
    }

    if (needs_preheader) {
      // also needs to updated cfg
      failure({"TODO: Impl preheader insertion", {cfg.bbrs[linfo.head].bb}});
      return false;

      TODO("TODO preheader insertion");
    }

    // if the header gets bbargs and these bb args are used *after* the loop
    //   we cant do a simple rotate since that might skip the loop leaving them
    //   undefined
    // Or in other terms the issue is a later bb needs to be dominated by the
    //   header but it wont after the rotate
    for (auto &arg : header_bb->args) {
      for (auto &use : arg.uses) {
        auto user_bb_id = cfg.get_bb_id(use.user->get_parent());
        if (std::find(linfo.body_nodes.begin(), linfo.body_nodes.end(),
                      user_bb_id) == linfo.body_nodes.end()) {

          failure({"Cannot handle loop rotate on loop whose header arguments "
                   "are used after the loop",
                   {use.user}});
          return false;
        }
      }
    }

    // now the header only got predecessors that use a simple jump to it

    // we need to figure out the headers_terminator which of the two targets is
    // the non exiting one
    uint8_t non_exiting_target = 1;
    {
      auto first_target = header_bb->get_terminator()->bbs[0].bb;
      for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
        if (cfg.bbrs[bb_id].bb == first_target &&
            std::find(linfo.body_nodes.begin(), linfo.body_nodes.end(),
                      bb_id) != linfo.body_nodes.end()) {
          non_exiting_target = 0;
          break;
        }
      }
    }

    // TODO: prob want to replace this map since its never gonna be more then a
    // handful of bb_block args
    TMap<fir::ValueR, fir::ValueR> repl_map;
    // replace all incoming branch instructions with the full header
    {
      // if we inserted preheader we can just replace it and all the tails
      // if not we can just replace all predecessors
      if (needs_preheader) {
        TODO("repl preheader + tails");
      } else {
        for (auto head_pred : cfg.bbrs[linfo.head].pred) {
          replace_branch_with_header(repl_map, non_exiting_target, header_bb,
                                     cfg.bbrs[head_pred].bb);
        }
      }
    }

    // then clean up the original header and just forward a jump into the
    // internal edge
    {
      fir::BasicBlock header = cfg.bbrs[linfo.head].bb;
      fir::Builder bb{header};
      // assert(header->get_terminator()->bbs[non_exiting_target].args.empty());

      auto old_header_term = header->get_terminator();
      auto new_header_term =
          bb.build_branch(old_header_term->bbs[non_exiting_target].bb);
      for (auto arg : old_header_term->bbs[non_exiting_target].args) {
        new_header_term.add_bb_arg(0, arg);
      }
      while (header->instructions.size() > 1) {
        header->remove_instr(header->instructions.size() - 1);
      }
    }

    // utils::Debug << cfg.bbrs[0].bb->get_parent() << "\n";
    // linfo.dump();
    // std::abort();
    return true;
  }
};
} // namespace foptim::optim
