#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/helper/helper.hpp"
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

    for (auto loop = linfo.info.begin(); loop != linfo.info.end(); loop++) {
      if (apply(ctx, cfg, *loop) && linfo.info.size() > 1) {
        cfg.update(func, false);
        dom.update(cfg);
        linfo.update(dom);
        loop = linfo.info.begin();
      }
    }
  }

  void replace_branch_with_header(TMap<fir::ValueR, fir::ValueR> &repl_map,
                                  uint8_t non_exiting_target,
                                  fir::BasicBlock header_bb,
                                  fir::BasicBlock head_pred) {
    repl_map.clear();
    fir::Instr old_terminator = head_pred->get_terminator();
    auto &old_terminator_args = old_terminator->bbs[0].args;

    // head_pred->remove_instr();

    // we need the replacement for the bb args in case they are used in
    // the header
    {
      size_t arg_id = 0;
      for (auto arg : old_terminator_args) {
        repl_map.insert({fir::ValueR(header_bb->args[arg_id]), arg});
        arg_id++;
      }
    }
    fir::Builder bb{head_pred};
    bb.at_end(head_pred);

    // IDK about this
    // since they are just inserted at the end we could just use the length
    //  FVec<fir::Instr> new_instrs;

    for (auto instr : header_bb->instructions) {
      fir::Instr new_instr = bb.insert_copy(instr);
      new_instr.substitute(repl_map);
      repl_map.insert({fir::ValueR(instr), fir::ValueR(new_instr)});
    }

    // clean up the arguments going into the loop based on the args of the old
    // terminator
    {
      fir::Instr new_term = head_pred->get_terminator();

      new_term.replace_bb(non_exiting_target, header_bb, false);
      for (auto arg : old_terminator_args) {
        new_term.add_bb_arg(non_exiting_target, arg);
      }
      flip_cond_branch(new_term);
    }

    old_terminator.remove_from_parent();
  }

  bool apply(fir::Context &ctx, const CFG &cfg, LoopInfo &linfo) {
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
    uint8_t exiting_target = 1 - non_exiting_target;

    // if the header gets bbargs and these bb args are used *after* the loop
    //   we cant do a simple rotate since that might skip the loop leaving them
    //   undefined

    TVec<fir::BBArgument> used_after_args;
    for (auto &arg : header_bb->args) {
      for (auto &use : arg->uses) {
        auto user_bb_id = cfg.get_bb_id(use.user->get_parent());
        if (std::find(linfo.body_nodes.begin(), linfo.body_nodes.end(),
                      user_bb_id) == linfo.body_nodes.end()) {
          used_after_args.push_back(arg);
          break;
        }
      }
    }

    if (!used_after_args.empty()) {
      // inserting en empty bb at the end which takes as bb args the header bb
      // args which need to be available later on
      //  and then replace all later uses of the bbargs with these new bbargs
      auto old_header = cfg.bbrs[linfo.head].bb;
      auto bb = fir::Builder(old_header);
      auto old_terminator = old_header->get_terminator();

      ASSERT(old_terminator->is(fir::InstrType::CondBranchInstr));

      auto &exit_target = old_terminator->bbs[exiting_target];

      auto new_exit = bb.append_bb();
      bb.at_end(new_exit);
      // contsruct our bb
      //  BB(old_exit_args..., used_after_args...)
      for (auto argy : exit_target.args) {
        new_exit.add_arg(ctx->storage.insert_bb_arg(new_exit, argy.get_type()));
      }
      TVec<fir::BBArgument> new_args;
      for (auto used_after : used_after_args) {
        auto new_arg = new_exit.add_arg(
            ctx->storage.insert_bb_arg(new_exit, used_after->get_type()));
        new_args.push_back(new_arg);
      }
      // add the terminator that forwards the old_exit_args
      auto new_exit_term = bb.build_branch(exit_target.bb);
      for (auto argy : exit_target.args) {
        new_exit_term.add_bb_arg(0, argy);
      }

      // update the header terminator to point to new exit + add the
      // used_after_args as bbargs
      old_terminator.replace_bb(exit_target.bb, new_exit, true);
      for (auto used_after : used_after_args) {
        old_terminator.add_bb_arg(exiting_target, fir::ValueR(used_after));
      }

      for (u32 arg_id = 0; arg_id < used_after_args.size(); arg_id++) {
        auto arg = used_after_args[arg_id];
        for (auto &use : arg->uses) {
          auto user_bb_id = cfg.get_bb_id(use.user->get_parent());
          if (std::find(linfo.body_nodes.begin(), linfo.body_nodes.end(),
                        user_bb_id) == linfo.body_nodes.end()) {
            use.replace_use(fir::ValueR(new_args[arg_id]));
            break;
          }
        }
      }

      // now replace the uses after with the new_exits bb_args
      // failure({"Cannot handle loop rotate on loop whose header arguments"
      //          "are used after the loop",
      //          {(*used_after_args.begin().base())->get_parent()}});
      // return false;
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
        header->remove_instr(header->instructions.size() - 1, true);
      }
    }
    // linfo.dump();
    // std::abort();
    return true;
  }
};
} // namespace foptim::optim
