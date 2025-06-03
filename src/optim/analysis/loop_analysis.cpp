#include "loop_analysis.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "utils/arena.hpp"
#include "utils/bitset.hpp"
#include "utils/logging.hpp"
#include "utils/vec.hpp"
#include <algorithm>
#include <fmt/core.h>

namespace foptim::optim {

void LoopInfoAnalysis::update(Dominators &dom) {
  info.clear();

  const CFG &cfg = *dom.cfg;
  TVec<u32> deq{};
  deq.reserve(32);

  utils::BitSet forward{cfg.bbrs.size(), false};
  utils::BitSet backward{cfg.bbrs.size(), false};

  // if a bb has a incoming edge from a block that it dominates we found a loop
  // header
  TVec<u32> tails;
  tails.reserve(32);
  for (u32 bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    tails.clear();
    for (auto pred : cfg.bbrs[bb_id].pred) {
      if (dom.dom_bbs[pred].dominators[bb_id]) {
        tails.push_back(pred);
      }
    }
    // we found an loop header + tails
    if (tails.empty()) {
      continue;
    }

    forward.reset(false);
    backward.reset(false);

    forward[bb_id].set(true);
    deq.clear();
    deq.push_back(bb_id);

    while (!deq.empty()) {
      u32 curr = deq.back();
      deq.pop_back();
      for (auto succ : cfg.bbrs[curr].succ) {
        if (!forward[succ]) {
          forward[succ].set(true);
          deq.push_back(succ);
        }
      }
    }

    TVec<u32> body_nodes;
    body_nodes.reserve(1 + 1 + tails.size() * 2);

    for (auto tail : tails) {
      backward[tail].set(true);
      body_nodes.push_back(tail);
      deq.push_back(tail);
    }

    // if one of the potential body nodes is not dominated by the header
    //  we do not consider this a real loop even though there are
    //  backwards edges
    // bool invalid_loop = false;

    while (!deq.empty()) {
      u32 curr = deq.back();
      deq.pop_back();
      for (auto pred : cfg.bbrs[curr].pred) {
        if (forward[pred] && !backward[pred] &&
            dom.dom_bbs[pred].dominators[bb_id]) {
          backward[pred].set(true);
          // if () {
          //   fmt::println(" INVALID {}", pred);
          //   invalid_loop = true;
          //   break;
          // }
          body_nodes.push_back(pred);
          deq.push_back(pred);
        }
      }
      // if (invalid_loop) {
      //   break;
      // }
    }
    // if (invalid_loop) {
    //   fmt::println("  INVALID");
    //   continue;
    // }

    TVec<u32> leaving_nodes;
    leaving_nodes.reserve(body_nodes.size() / 2);
    for (u32 node : body_nodes) {
      for (auto succ : cfg.bbrs[node].succ) {
        if (std::find(body_nodes.begin(), body_nodes.end(), succ) ==
            body_nodes.end()) {
          leaving_nodes.push_back(node);
        }
      }
    }

    info.push_back({
        bb_id,
        tails,
        std::move(body_nodes),
        std::move(leaving_nodes),
    });
  }
}

void LoopInfo::dump() const {
  fmt::println("Loop\n  Header: {}\n  Body: {}\n  Tails: {}\n  Leaving: {}\n",
               head, body_nodes, tails, leaving_nodes);
}
void LoopInfoAnalysis::dump() const {
  for (const auto &loop : info) {
    loop.dump();
  }
}

void LoopRangeAnalysis::dump() const {
  fmt::print("{}  ", induction_var);
  if (known_lower) {
    fmt::print("{}", lower_bound_var);
    fmt::print("@{}", lower_bound);
  }
  fmt::print("..");
  if (known_upper) {
    fmt::print("{}", upper_bound_var);
    fmt::print("@{}", upper_bound);
  }
  fmt::println(" step: {}", a);
}

bool LoopRangeAnalysis::update(CFG &cfg, LoopInfo &info) {
  fir::BasicBlock head = cfg.bbrs[info.head].bb;
  // info.dump();
  // exactly 1 induction var
  if (head->args.size() != 1) {
    return false;
  }
  // only 1 incoming edge into the loop
  if (cfg.bbrs[info.head].pred.size() != 1 + info.tails.size()) {
    fmt::println("0");
    return false;
  }
  for (u32 p : cfg.bbrs[info.head].pred) {
    if (std::find(info.body_nodes.begin(), info.body_nodes.end(), p) ==
        info.body_nodes.end()) {
      auto incoming_bb_term = cfg.bbrs[p].bb->get_terminator();
      auto outgoing_id = incoming_bb_term.get_bb_id(head);
      auto arg = incoming_bb_term->bbs[outgoing_id].args[0];
      if (arg.is_constant() && arg.as_constant()->is_int()) {
        known_lower = true;
        lower_bound = arg.as_constant()->as_int();
        lower_bound_var = fir::Use::bb_arg(incoming_bb_term, outgoing_id, 0);
      }
      break;
    }
  }

  auto induct_incoming = head->args[0];
  induction_var = induct_incoming;

  bool set = false;
  i128 constant_off = 0;
  fir::ValueR induct_outgoing = fir::ValueR();

  for (auto tail : info.tails) {
    auto tail_bb = cfg.bbrs[tail].bb;
    auto term = tail_bb->get_terminator();
    if (tail_bb != head || !term->is(fir::InstrType::CondBranchInstr)) {
      // fmt::println("1");
      return false;
    }
    // there can only be 1 arg
    ASSERT(term->bbs[0].bb == head);
    auto induction_arg = term->bbs[0].args[0];
    // TODO: improve
    if (!induction_arg.is_instr()) {
      // fmt::println("2");
      return false;
    }
    auto induct_oper = induction_arg.as_instr();
    if (!induct_oper->is(fir::InstrType::BinaryInstr) ||
        (fir::BinaryInstrSubType)induct_oper->subtype !=
            fir::BinaryInstrSubType::IntAdd) {
      // fmt::println("3");
      return false;
    }

    u8 var_index = 0;
    u8 constant_index = 0;
    if (induct_oper->args[0].is_constant()) {
      var_index = 1;
      constant_index = 0;
    } else if (induct_oper->args[1].is_constant()) {
      var_index = 0;
      constant_index = 1;
    } else {
      // fmt::println("4a");
      return false;
    }
    auto con = induct_oper->args[constant_index].as_constant();
    auto var = induct_oper->args[var_index];
    if (!con->is_int()) {
      // fmt::println("4");
      return false;
    }
    if (!var.is_bb_arg() || var.as_bb_arg() != induct_incoming) {
      // fmt::println("5");
      return false;
    }

    auto new_off = con->as_int();
    if (set && constant_off != new_off) {
      // fmt::println("6");
      return false;
    }

    constant_off = new_off;
    induct_outgoing = induction_arg;
    set = true;
  }
  a = constant_off;

  // find the upper bound
  {
    auto term = head->get_terminator();
    ASSERT(term->is(fir::InstrType::CondBranchInstr));
    auto cond = term->args[0];
    if (!cond.is_instr() || !cond.as_instr()->is(fir::InstrType::ICmp)) {
      // fmt::println("7");
      return false;
    }
    auto cond_instr = cond.as_instr();
    auto condi = (fir::ICmpInstrSubType)cond_instr->subtype;
    if (condi != fir::ICmpInstrSubType::ULT &&
        condi != fir::ICmpInstrSubType::SLT) {
      // fmt::println("8");
      return false;
    }

    u8 var_index = 0;
    u8 constant_index = 0;
    if (cond_instr->args[0].is_constant()) {
      var_index = 1;
      constant_index = 0;
    } else if (cond_instr->args[1].is_constant()) {
      var_index = 0;
      constant_index = 1;
    } else {
      // fmt::println("9a");
      return false;
    }
    auto con = cond_instr->args[constant_index].as_constant();
    auto var = cond_instr->args[var_index];
    if (!con->is_int()) {
      // fmt::println("9");
      return false;
    }
    if (var != induct_outgoing) {
      // fmt::println("10");
      return false;
    }

    auto new_off = con->as_int();
    known_upper = true;
    upper_bound = new_off;
    upper_bound_var = fir::Use::norm(cond_instr, constant_index);
  }

  return true;
}

std::optional<InductionVarAnalysis::InductionVar>
InductionVarAnalysis::_check_if_direct_induct(
    fir::BBArgument v, u32 arg_id,
    TVec<std::pair<fir::Instr, u32>> backwards_jumps, CFG &cfg,
    LoopInfo &info) {
  (void)info;
  (void)cfg;
  // go along usedef chain upwards from the tails and check if argument at
  // location i always depends on
  //  the same bbarg in the header + constant or - constant
  bool first = true;
  InductionVar var{fir::ValueR{v},
                   fir::ConstantValueR{fir::ConstantValueR::invalid()},
                   IterationType::Other};
  for (auto &backwards : backwards_jumps) {
    auto backv = backwards.first->bbs[backwards.second].args[arg_id];
    if (backv.is_instr() && backv.as_instr()->is(fir::InstrType::BinaryInstr)) {
      auto i = backv.as_instr();
      auto subty = (fir::BinaryInstrSubType)i->subtype;
      if ((subty == fir::BinaryInstrSubType::IntAdd ||
           subty == fir::BinaryInstrSubType::IntSub) &&
          i->args[0].is_bb_arg() && i->args[1].is_constant()) {
        if (i->args[0].as_bb_arg() != v) {
          return {};
        }
        auto new_iter_ty = subty == fir::BinaryInstrSubType::IntAdd
                               ? IterationType::PlusConst
                               : IterationType::SubConst;
        auto new_const = i->args[1].as_constant();
        if (first) {
          first = false;
          var.consti = new_const;
          var.type = new_iter_ty;
        } else if (var.consti != new_const || var.type != new_iter_ty) {
          fmt::println("Failed1 induct {}", v);
          return {};
        }
      }
    } else {
      fmt::println("Failed2 induct {}", v);
      return {};
    }
  }
  return var;
}

void InductionVarAnalysis::update(CFG &cfg, LoopInfo &info) {
  ZoneScopedN("Induct var UPDATE");
  direct_inductvars.clear();
  indirect_inductvars.clear();

  TVec<std::pair<fir::Instr, u32>> backwards_jumps;
  for (auto tail : info.tails) {
    auto term = cfg.bbrs[tail].bb->get_terminator();
    auto term_id = 0;
    for (auto &target_bb : term->bbs) {
      auto target_bb_id = cfg.get_bb_id(target_bb.bb);
      if (std::find(info.body_nodes.begin(), info.body_nodes.end(),
                    target_bb_id) != info.body_nodes.end()) {
        backwards_jumps.emplace_back(term, term_id);
      }
      term_id++;
    }
  }

  auto &header = cfg.bbrs[info.head];
  u32 arg_id = 0;
  for (auto arg : header.bb->args) {
    if (!arg->get_type()->is_int() && !arg->get_type()->is_ptr()) {
      continue;
    }
    if (auto v =
            _check_if_direct_induct(arg, arg_id, backwards_jumps, cfg, info)) {
      direct_inductvars.push_back(v.value());
    }
    arg_id++;
  }

  TVec<fir::ValueR> worklist;
  worklist.reserve(direct_inductvars.size() * 2);
  for (auto i : direct_inductvars) {
    worklist.push_back(i.def);
  }
  while (!worklist.empty()) {
    auto f = worklist.back();
    worklist.pop_back();
    for (auto &use : *f.get_uses()) {
      if (use.user->is(fir::InstrType::BinaryInstr) &&
          use.type == fir::UseType::NormalArg && use.argId == 0) {
        if (use.user->args[1].is_constant()) {
          switch ((fir::BinaryInstrSubType)use.user->subtype) {
          case fir::BinaryInstrSubType::IntAdd:
            indirect_inductvars.push_back({fir::ValueR{use.user},
                                           use.user->args[0], use.user->args[1],
                                           IterationType::PlusConst});
            break;
          case fir::BinaryInstrSubType::IntSub:
            indirect_inductvars.push_back({fir::ValueR{use.user},
                                           use.user->args[0], use.user->args[1],
                                           IterationType::SubConst});
            break;
          case fir::BinaryInstrSubType::IntMul:
            indirect_inductvars.push_back({fir::ValueR{use.user},
                                           use.user->args[0], use.user->args[1],
                                           IterationType::MulConst});
            break;
          default:
            break;
          }
        }
      }
    }
  }
}

void InductionVarAnalysis::dump() const {
  fmt::println("Induction vars: ");
  for (const auto &ind : direct_inductvars) {
    fmt::print("  {}", ind.def);
    switch (ind.type) {
    case PlusConst:
      fmt::println(" + {}", ind.consti);
      break;
    case SubConst:
      fmt::println(" - {}", ind.consti);
      break;
    case MulConst:
      fmt::println(" * {}", ind.consti);
      break;
    case Other:
      fmt::println(" ?\n");
      break;
    }
  }
  for (const auto &ind : indirect_inductvars) {
    fmt::print("  {} = ", ind.def);
    switch (ind.type) {
    case PlusConst:
      fmt::println("{} + {}", ind.arg1, ind.arg2);
      break;
    case SubConst:
      fmt::println("{} - {}", ind.arg1, ind.arg2);
      break;
    case MulConst:
      fmt::println("{} * {}", ind.arg1, ind.arg2);
      break;
    case Other:
      fmt::println("{} ? {}", ind.arg1, ind.arg2);
      break;
    }
  }
}

} // namespace foptim::optim
