#include "helper.hpp"

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "optim/analysis/dominators.hpp"
#include <fmt/base.h>
#include <functional>

namespace foptim::optim {

void swap_args(fir::Instr instr, u32 a1, u32 a2) {
  auto v1 = instr->args[a1];
  auto v2 = instr->args[a2];
  instr.replace_arg(a1, v2);
  instr.replace_arg(a2, v1);
}

void flip_cond_branch(fir::Instr cond_term) {
  ASSERT(cond_term->is(fir::InstrType::CondBranchInstr));

  auto builder = cond_term->parent.builder();
  builder.at_penultimate(cond_term->parent);

  auto negated_value =
      builder.build_unary_op(cond_term->args[0], fir::UnaryInstrSubType::Not);

  cond_term.replace_arg(0, negated_value);

  auto arg0 = cond_term->bbs[0];
  auto arg1 = cond_term->bbs[1];

  cond_term.replace_bb(0, arg1.bb, false, false);
  for (auto arg : arg1.args) {
    cond_term.add_bb_arg(0, arg);
  }
  cond_term.replace_bb(1, arg0.bb, false, false);
  for (auto arg : arg0.args) {
    cond_term.add_bb_arg(1, arg);
  }
}

GuessTypeResult guessType(fir::ValueR ptr) {
  if (ptr.is_constant()) {
    return {.typeless = true, .type = fir::TypeR{fir::TypeR::invalid()}};
  }
  if (ptr.is_bb_arg()) {
    // auto bb_arg = ptr.as_bb_arg();
    // if (bb_arg->_parent == bb_arg->_parent->get_parent()->get_entry()) {
    return {.typeless = true, .type = fir::TypeR{fir::TypeR::invalid()}};
    // }
  }
  if (ptr.is_instr()) {
    auto ptr_instr = ptr.as_instr();
    if (ptr_instr->is(fir::InstrType::AllocaInstr)) {
      if (ptr_instr->extra_type.is_valid()) {
        return {.typeless = false, .type = ptr_instr->extra_type};
      }
      return {.typeless = true, .type = fir::TypeR{fir::TypeR::invalid()}};
    }
    if (ptr_instr->is(fir::InstrType::LoadInstr)) {
      return {.typeless = true, .type = fir::TypeR{fir::TypeR::invalid()}};
    }
    if (ptr_instr->is(fir::BinaryInstrSubType::IntAdd) ||
        ptr_instr->is(fir::BinaryInstrSubType::PtrAdd)) {
      GuessTypeResult out_res = guessType(ptr_instr->args[0]);
      GuessTypeResult r2 = guessType(ptr_instr->args[1]);
      if (out_res.typeless && !r2.typeless) {
        return r2;
      }
      if (!out_res.typeless && !r2.typeless) {
        return {.typeless = false, .type = fir::TypeR{fir::TypeR::invalid()}};
      }
      return out_res;
    }
  }
  return {.typeless = false, .type = fir::TypeR{fir::TypeR::invalid()}};
}

bool cond_tail_duplication(foptim::fir::Context &ctx, foptim::optim::CFG &cfg,
                           foptim::optim::Dominators &dom,
                           fir::BasicBlock cond_bb, fir::Instr term,
                           IncomingData inc2) {
  fir::Builder bb{cond_bb};
  fir::ContextData::V2VMap subs;
  auto copy = bb.insert_copy(cond_bb, subs);
  inc2.term.replace_bb(inc2.inc_bb_id, copy);

  // Collect values defined in cond_bb that have uses outside cond_bb (and
  // therefore also outside `copy`, since `copy` is a clone). Each such value
  // now has two live definitions post-split (original in cond_bb, clone in
  // copy) reaching those external uses, so they must be merged.
  TVec<fir::ValueR> to_merge;
  for (auto &arg : cond_bb->args) {
    bool needs_merge = false;
    for (const auto &use : arg->get_uses()) {
      if (use.user->get_parent() != cond_bb) {
        needs_merge = true;
        break;
      }
    }
    if (needs_merge) {
      to_merge.emplace_back(arg);
    }
  }
  for (auto &instr : cond_bb->instructions) {
    bool needs_merge = false;
    for (const auto &use : instr->get_uses()) {
      if (use.user->get_parent() != cond_bb) {
        needs_merge = true;
        break;
      }
    }
    if (needs_merge) {
      to_merge.emplace_back(instr);
    }
  }

  if (!to_merge.empty()) {
    // Create a merge block that both cond_bb and copy will branch to,
    // taking over cond_bb's outgoing edges (its current terminator/successors).
    auto merge1_bb = bb.append_bb();
    auto merge2_bb = bb.append_bb();
    TVec<fir::BBArgument> merge_args[2];

    auto setup_merge_bb = [&bb, &to_merge, &ctx, &term,
                           &merge_args](fir::BasicBlock target_bb, size_t off) {
      bb.at_end(target_bb);
      merge_args[off].reserve(to_merge.size());
      for (auto v : to_merge) {
        merge_args[off].push_back(target_bb.add_arg(
            ctx.data->storage.insert_bb_arg(target_bb, v.get_type())));
      }
      auto new_b = bb.build_branch(term->bbs[off].bb);
      for (auto arg : term->bbs[off].args) {
        new_b.add_arg(arg);
      }
    };
    setup_merge_bb(merge1_bb, 0);
    setup_merge_bb(merge2_bb, 1);
    // Update the old branches
    {
      bb.at_end(cond_bb);
      auto old_term = cond_bb->get_terminator();
      auto branch =
          bb.build_cond_branch(old_term->args[0], merge1_bb, merge2_bb);
      for (auto arg : to_merge) {
        branch.add_bb_arg(0, arg);
        branch.add_bb_arg(1, arg);
      }
      old_term.destroy();
    };
    {
      bb.at_end(copy);
      auto old_term = copy->get_terminator();
      auto branch =
          bb.build_cond_branch(old_term->args[0], merge1_bb, merge2_bb);
      for (auto arg : to_merge) {
        branch.add_bb_arg(0, subs.at(arg));
        branch.add_bb_arg(1, subs.at(arg));
      }
      old_term.destroy();
    };

    // fixup the uses that happen later
    //  for that we need to know who dominates them and for that we need to
    //  update these things
    {
      auto *func = cfg.func;
      cfg = {};
      dom = {};
      cfg = CFG(*func, false);
      dom = Dominators(cfg);
    }

    auto term1bb_id = cfg.get_bb_id(merge1_bb);
    auto term2bb_id = cfg.get_bb_id(merge2_bb);

    for (size_t i = 0; i < to_merge.size(); ++i) {
      fir::ValueR orig = to_merge[i];
      fir::ValueR merged1_arg = fir::ValueR{merge_args[0][i]};
      fir::ValueR merged2_arg = fir::ValueR{merge_args[1][i]};

      // Collect external uses first to avoid iterator invalidation while
      // modifying
      TVec<fir::Use> ext_uses;
      for (const auto &use : *orig.get_uses()) {
        if (use.user->get_parent() != cond_bb) {
          ext_uses.push_back(use);
        }
      }

      TVec<fir::Use> non_dominated_uses;
      // Replace uses based on which path they belong to
      for (auto &use : ext_uses) {
        auto user_bb = use.user->get_parent();
        auto user_bb_id = cfg.get_bb_id(user_bb);
        if (dom.dominates(term1bb_id, user_bb_id)) {
          use.replace_use(merged1_arg);
        } else if (dom.dominates(term2bb_id, user_bb_id)) {
          use.replace_use(merged2_arg);
        } else {
          non_dominated_uses.push_back(use);
        }
      }

      // if we have a non dominated use then we need to add a bbarg to its bb
      // replae the use with that bbarg
      //  then update all incoming branches to then try to update their
      //  terminator with one of our new incoming vlaues if we dont dominaote
      //  one of those push it back into non dominted uses and continue till all
      //  are reoslved
      if (!non_dominated_uses.empty()) {
        TMap<u32, fir::ValueR> resolved;
        resolved[term1bb_id] = merged1_arg;
        resolved[term2bb_id] = merged2_arg;

        std::function<fir::ValueR(u32)> resolve_value =
            [&](u32 bb_id) -> fir::ValueR {
          if (auto it = resolved.find(bb_id); it != resolved.end()) {
            return it->second;
          }
          if (dom.dominates(term1bb_id, bb_id)) {
            return resolved[bb_id] = merged1_arg;
          }
          if (dom.dominates(term2bb_id, bb_id)) {
            return resolved[bb_id] = merged2_arg;
          }

          // Different predecessor paths may carry different values here, so
          // this block needs its own bb-arg
          fir::BasicBlock target_bb = cfg.bbrs[bb_id].bb;
          auto arg_storage =
              ctx.data->storage.insert_bb_arg(target_bb, orig.get_type());
          auto new_arg = target_bb.add_arg(arg_storage);
          fir::ValueR new_val{new_arg};
          // Register before recursing into preds so a cycle resolves to this
          // same not yet ready value instead of looping forever.
          resolved[bb_id] = new_val;

          for (u32 pred_id : cfg.bbrs[bb_id].pred) {
            fir::ValueR pred_val = resolve_value(pred_id);
            fir::Instr pred_term = cfg.bbrs[pred_id].bb->get_terminator();
            // A terminator can target bb_id via more than one successor edge
            // (e.g. both arms of a cond branch aimed at the same block), so
            // patch every matching edge.
            for (size_t edge = 0; edge < pred_term->bbs.size(); ++edge) {
              if (cfg.get_bb_id(pred_term->bbs[edge].bb) == bb_id) {
                pred_term.add_bb_arg(edge, pred_val);
              }
            }
          }
          return new_val;
        };

        for (auto &use : non_dominated_uses) {
          auto user_bb_id = cfg.get_bb_id(use.user->get_parent());
          use.replace_use(resolve_value(user_bb_id));
        }
      }
    }
  }
  return true;
}

} // namespace foptim::optim
