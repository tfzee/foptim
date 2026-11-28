#include "split_cond_branch.hpp"

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

namespace {
using fir::BinaryInstrSubType;
using fir::InstrType;

bool is_cond_tree(fir::ValueR v, fir::BasicBlock bb) {
  if (!v.is_instr() || v.get_n_uses() != 1 || v.get_type()->is_vec()) {
    return false;
  }
  auto instr = v.as_instr();
  if (instr->get_parent() != bb) {
    return false;
  }
  return instr->is(InstrType::ICmp) || instr->is(InstrType::FCmp) ||
         instr->is(BinaryInstrSubType::And) ||
         instr->is(BinaryInstrSubType::Or);
}

// a load that only feeds a compare can follow it when nothing after the load
// writes memory
bool can_sink_load(fir::Instr load, fir::BasicBlock bb) {
  if (!load->is(InstrType::LoadInstr) || load->Atomic || load->Volatile) {
    return false;
  }
  auto it = std::ranges::find(bb->instructions, load);
  for (; it != bb->instructions.end(); ++it) {
    if (*it != load &&
        ((*it)->pot_modifies_mem() || (*it)->has_pot_sideeffects())) {
      return false;
    }
  }
  return true;
}

// the instructions that move into the second block: the condition, the
// conditions below it and the loads that only feed a compare
void collect_cond_tree(fir::Instr instr, fir::BasicBlock bb,
                       TSet<fir::Instr> &out) {
  out.insert(instr);
  const bool is_cmp = instr->is(InstrType::ICmp) || instr->is(InstrType::FCmp);
  for (auto arg : instr->args) {
    if (!arg.is_instr() || arg.get_n_uses() != 1 ||
        arg.as_instr()->get_parent() != bb) {
      continue;
    }
    auto arg_instr = arg.as_instr();
    if (is_cmp) {
      if (can_sink_load(arg_instr, bb)) {
        out.insert(arg_instr);
      }
    } else if (is_cond_tree(arg, bb)) {
      collect_cond_tree(arg_instr, bb, out);
    }
  }
}
}  // namespace

PreservedAnalysis SplitCondBranch::apply(fir::Context & /*ctx*/,
                                         fir::Function &func) {
  bool changed = false;
  TVec<fir::BasicBlock> work(func.basic_blocks.begin(),
                             func.basic_blocks.end());
  while (!work.empty()) {
    auto bb = work.back();
    work.pop_back();
    auto term = bb->get_terminator();
    if (!term->is(InstrType::CondBranchInstr) || term->bbs.size() != 2 ||
        term->bbs[0].bb == term->bbs[1].bb) {
      continue;
    }
    auto cond = term->args[0];
    if (!cond.is_instr() || cond.get_n_uses() != 1 ||
        cond.as_instr()->get_parent() != bb || cond.get_type()->is_vec()) {
      continue;
    }
    auto cond_instr = cond.as_instr();
    const bool is_and = cond_instr->is(BinaryInstrSubType::And);
    if (!is_and && !cond_instr->is(BinaryInstrSubType::Or)) {
      continue;
    }
    auto c1 = cond_instr->args[0];
    auto c2 = cond_instr->args[1];
    if (!is_cond_tree(c1, bb) || !is_cond_tree(c2, bb)) {
      continue;
    }

    TSet<fir::Instr> moved;
    collect_cond_tree(c2.as_instr(), bb, moved);
    auto true_bb = term->bbs[0].bb;
    auto false_bb = term->bbs[1].bb;
    const TVec<fir::ValueR> true_args(term->bbs[0].args.begin(),
                                      term->bbs[0].args.end());
    const TVec<fir::ValueR> false_args(term->bbs[1].args.begin(),
                                       term->bbs[1].args.end());

    fir::Builder b{bb};
    auto next_bb = b.append_bb();
    b.at_end(next_bb);
    const TVec<fir::Instr> instrs(bb->instructions.begin(),
                                  bb->instructions.end());
    for (auto instr : instrs) {
      if (moved.contains(instr)) {
        b.move_instr(instr);
      }
    }
    auto next_term = b.build_cond_branch(c2, true_bb, false_bb);
    for (auto arg : true_args) {
      next_term.add_bb_arg(0, arg);
    }
    for (auto arg : false_args) {
      next_term.add_bb_arg(1, arg);
    }

    // and continues on true, or continues on false
    term.replace_bb(is_and ? 0 : 1, next_bb, false);
    term.replace_arg(0, c1);
    cond_instr.destroy();
    changed = true;
    work.push_back(bb);
    work.push_back(next_bb);
  }
  return changed ? PreservedAnalysis::none() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
