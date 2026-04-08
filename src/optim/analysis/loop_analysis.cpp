#include <fmt/core.h>

#include <algorithm>

#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "loop_analysis.hpp"
#include "utils/bitset.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

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
      if (dom.dominates(pred, bb_id)) {
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
    body_nodes.reserve(2 + (tails.size() * 2));

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
            dom.dominates(pred, bb_id)) {
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
        if (std::ranges::find(body_nodes, succ) == body_nodes.end()) {
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
  constexpr bool debug_print = false;
  // info.dump();
  // need 1 induction var
  if (head->args.empty()) {
    // just gonna assume the first for now
    if constexpr (debug_print) {
      fmt::println("0a");
    }
    return false;
  }
  // only 1 incoming edge into the loop
  if (cfg.bbrs[info.head].pred.size() != 1 + info.tails.size()) {
    if constexpr (debug_print) {
      fmt::println("0");
    }
    return false;
  }
  for (u32 p : cfg.bbrs[info.head].pred) {
    if (std::ranges::find(info.body_nodes, p) == info.body_nodes.end()) {
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
      if constexpr (debug_print) {
        fmt::println("1");
      }
      return false;
    }
    // there can only be 1 arg
    ASSERT(term->bbs[0].bb == head);
    auto induction_arg = term->bbs[0].args[0];
    // TODO: improve
    if (!induction_arg.is_instr()) {
      if constexpr (debug_print) {
        fmt::println("2");
      }
      return false;
    }
    auto induct_oper = induction_arg.as_instr();
    if (!induct_oper->is(fir::InstrType::BinaryInstr) ||
        (fir::BinaryInstrSubType)induct_oper->subtype !=
            fir::BinaryInstrSubType::IntAdd) {
      if constexpr (debug_print) {
        fmt::println("3");
      }
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
      if constexpr (debug_print) {
        fmt::println("4a");
      }
      return false;
    }
    auto con = induct_oper->args[constant_index].as_constant();
    auto var = induct_oper->args[var_index];
    if (!con->is_int()) {
      if constexpr (debug_print) {
        fmt::println("4");
      }
      return false;
    }
    if (!var.is_bb_arg() || var.as_bb_arg() != induct_incoming) {
      if constexpr (debug_print) {
        fmt::println("5");
      }
      return false;
    }

    auto new_off = con->as_int();
    if (set && constant_off != new_off) {
      if constexpr (debug_print) {
        fmt::println("6");
      }
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
      if constexpr (debug_print) {
        fmt::println("7");
      }
      return false;
    }
    auto cond_instr = cond.as_instr();
    auto condi = (fir::ICmpInstrSubType)cond_instr->subtype;
    if (condi != fir::ICmpInstrSubType::ULT &&
        condi != fir::ICmpInstrSubType::SLT) {
      if constexpr (debug_print) {
        fmt::println("8");
      }
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
      if constexpr (debug_print) {
        fmt::println("9a");
      }
      return false;
    }
    auto con = cond_instr->args[constant_index].as_constant();
    auto var = cond_instr->args[var_index];
    if (!con->is_int()) {
      if (debug_print) {
        fmt::println("9");
      }
      return false;
    }
    if (var != induct_outgoing) {
      if constexpr (debug_print) {
        fmt::println("10");
      }
      return false;
    }

    auto new_off = con->as_int();
    known_upper = true;
    upper_bound = new_off;
    upper_bound_var = fir::Use::norm(cond_instr, constant_index);
  }
  if constexpr (debug_print) {
    fmt::println("f");
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

  bool first = true;
  InductionVar var{
      .def = fir::ValueR{v},
      .consti = fir::ConstantValueR{fir::ConstantValueR::invalid()},
      .type = IterationType::Other,
  };

  for (auto &[term, target_idx] : backwards_jumps) {
    if (arg_id >= term->bbs[target_idx].args.size()) {
      continue;
    }
    auto backv = term->bbs.at(target_idx).args.at(arg_id);

    if (!backv.is_instr()) {
      return std::nullopt;
    }

    auto i = backv.as_instr();
    if (!i->is(fir::InstrType::BinaryInstr)) {
      return std::nullopt;
    }

    auto subty = static_cast<fir::BinaryInstrSubType>(i->subtype);

    if ((subty != fir::BinaryInstrSubType::IntAdd &&
         subty != fir::BinaryInstrSubType::IntSub) ||
        (!i->args[0].is_bb_arg() && !i->args[1].is_bb_arg()) ||
        (!i->args[0].is_constant() && !i->args[1].is_constant())) {
      return std::nullopt;
    }

    // Normalize operands: `v +/- c` or `c +/- v` (commutative for Add)
    fir::ValueR var_arg;
    fir::ConstantValueR const_arg{fir::ConstantValueR::invalid()};
    IterationType type = IterationType::Other;

    if (i->args[0].is_bb_arg() && i->args[0].as_bb_arg() == v &&
        i->args[1].is_constant()) {
      var_arg = i->args[0];
      const_arg = i->args[1].as_constant();
      type = (subty == fir::BinaryInstrSubType::IntAdd)
                 ? IterationType::PlusConst
                 : IterationType::SubConst;
    } else if (subty == fir::BinaryInstrSubType::IntAdd &&
               i->args[1].is_bb_arg() && i->args[1].as_bb_arg() == v &&
               i->args[0].is_constant()) {
      // Commutative Add: allow `c + v`
      var_arg = i->args[1];
      const_arg = i->args[0].as_constant();
      type = IterationType::PlusConst;
    } else {
      return std::nullopt;
    }

    if (first) {
      first = false;
      var.consti = const_arg;
      var.type = type;
    } else {
      if (!var.consti.is_valid() || !const_arg.is_valid()) {
        return std::nullopt;
      }

      if (var.consti->as_int() != const_arg->as_int() || var.type != type) {
        return std::nullopt;
      }
    }
  }

  return var;
}

void InductionVarAnalysis::update(CFG &cfg, LoopInfo &info) {
  ZoneScopedN("Induct var UPDATE");

  direct_inductvars.clear();
  indirect_inductvars.clear();

  // --- Collect backward jumps for loop header phi analysis ---
  TVec<std::pair<fir::Instr, u32>> backwards_jumps;
  TSet<u32> body_node_set(info.body_nodes.begin(), info.body_nodes.end());

  for (auto tail : info.tails) {
    auto term = cfg.bbrs[tail].bb->get_terminator();
    for (u32 i = 0; i < term->bbs.size(); ++i) {
      const auto &target_bb = term->bbs[i];
      u32 target_id = cfg.get_bb_id(target_bb.bb);
      if (body_node_set.contains(target_id)) {
        backwards_jumps.emplace_back(term, i);
      }
    }
  }

  // --- Identify direct induction variables from loop header phi args ---
  auto &header = cfg.bbrs[info.head];
  for (u32 arg_id = 0; arg_id < header.bb->args.size(); ++arg_id) {
    auto arg = header.bb->args[arg_id];
    if (!arg->get_type()->is_int() && !arg->get_type()->is_ptr()) {
      continue;
    }

    if (auto v =
            _check_if_direct_induct(arg, arg_id, backwards_jumps, cfg, info)) {
      direct_inductvars.push_back(*v);
    }
  }

  // --- Collect indirect induction variables from BinaryInstrs ---
  TVec<fir::ValueR> worklist;
  TSet<fir::ValueR> seen;
  for (const auto &d : direct_inductvars) {
    worklist.push_back(d.def);
    seen.insert(d.def);
  }

  while (!worklist.empty()) {
    fir::ValueR current = worklist.back();
    worklist.pop_back();

    auto *uses = current.get_uses();
    if (uses == nullptr) {
      continue;
    }

    for (const auto &use : *uses) {
      auto user = use.user;

      // Only consider binary operations on normal arg0
      if (!user->is(fir::InstrType::BinaryInstr) ||
          use.type != fir::UseType::NormalArg || use.argId != 0) {
        continue;
      }

      // Check second arg is constant
      const auto &rhs = user->args[1];
      if (!rhs.is_constant()) {
        continue;
      }

      auto subtype = static_cast<fir::BinaryInstrSubType>(user->subtype);
      IterationType itype = IterationType::Other;

      switch (subtype) {
        case fir::BinaryInstrSubType::IntAdd:
          itype = IterationType::PlusConst;
          break;
        case fir::BinaryInstrSubType::IntSub:
          itype = IterationType::SubConst;
          break;
        case fir::BinaryInstrSubType::IntMul:
          itype = IterationType::MulConst;
          break;
        default:
          continue;
      }

      fir::ValueR result{user};
      if (!seen.contains(result)) {
        indirect_inductvars.push_back({.def = result,
                                       .arg1 = user->args[0],
                                       .arg2 = user->args[1],
                                       .type = itype});
        worklist.push_back(result);
        seen.insert(result);
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

namespace {
std::optional<i128> get_constant(fir::ValueR v) {
  return (v.is_constant() && v.as_constant()->is_int())
             ? std::optional{i128(v.as_constant()->as_int())}
             : std::nullopt;
}

std::optional<InductionVarAnalysis::InductionVar> resolve_base_induction(
    const fir::ValueR &v, const InductionVarAnalysis &ianal, i128 &acc_offset) {
  auto current = std::ranges::find_if(
      ianal.indirect_inductvars, [&](const auto &iv) { return iv.def == v; });

  if (current == ianal.indirect_inductvars.end() ||
      ((current->type != InductionVarAnalysis::PlusConst &&
        current->type != InductionVarAnalysis::SubConst) ||
       !current->arg2.is_constant())) {
    return {};
  }

  while (current != ianal.indirect_inductvars.end()) {
    i128 delta = current->arg2.as_constant()->as_int();
    if (current->type == InductionVarAnalysis::PlusConst) {
      acc_offset += delta;
    } else if (current->type == InductionVarAnalysis::SubConst) {
      acc_offset -= delta;
    }

    const auto &parent_v = current->arg1;

    auto neww =
        std::ranges::find_if(ianal.indirect_inductvars, [&](const auto &iv) {
          return iv.def == parent_v &&
                 (iv.type == InductionVarAnalysis::PlusConst ||
                  iv.type == InductionVarAnalysis::SubConst) &&
                 iv.arg2.is_constant();
        });
    if (neww == ianal.indirect_inductvars.end()) {
      break;
    }
    current = neww;
  }

  auto base = std::ranges::find_if(
      ianal.direct_inductvars,
      [&](const auto &dv) { return dv.def == current->arg1; });

  if (base != ianal.direct_inductvars.end()) {
    return *base;
  }

  return std::nullopt;
}
}  // namespace

void InductionEndValueAnalysis::update(CFG &cfg, LoopInfo &linfo,
                                       InductionVarAnalysis &ianal) {
  info.clear();

  TSet<fir::BasicBlock> loop_bbs;
  loop_bbs.reserve(linfo.body_nodes.size());
  for (auto node : linfo.body_nodes) {
    loop_bbs.insert(cfg.bbrs[node].bb);
  }

  for (auto leav : linfo.leaving_nodes) {
    auto &lbb = cfg.bbrs[leav];
    if (lbb.succ.size() != 2) {
      continue;
    }

    auto term = lbb.bb->get_terminator();
    fir::BasicBlock out_bb = term->bbs[1].bb;
    if (loop_bbs.contains(out_bb)) {
      continue;
    }

    auto condv = term->args[0];
    if (!condv.is_instr()) {
      continue;
    }

    auto cond = condv.as_instr();
    if (!cond->is(fir::InstrType::ICmp)) {
      continue;
    }

    auto subtype = static_cast<fir::ICmpInstrSubType>(cond->subtype);
    bool is_leq_type = subtype == fir::ICmpInstrSubType::SLE ||
                       subtype == fir::ICmpInstrSubType::ULE;
    bool is_lt_type = subtype == fir::ICmpInstrSubType::SLT ||
                      subtype == fir::ICmpInstrSubType::ULT;

    if (!is_leq_type && !is_lt_type) {
      continue;
    }

    const fir::ValueR &induct_candidate = cond->args[0];
    const fir::ValueR &limit_candidate = cond->args[1];

    auto cmp_val_opt = get_constant(limit_candidate);
    if (!cmp_val_opt) {
      continue;
    }

    i128 cmp_val = *cmp_val_opt;
    i128 offset = 0;

    auto base_induct_opt =
        resolve_base_induction(induct_candidate, ianal, offset);
    if (!base_induct_opt.has_value()) {
      continue;
    }

    const auto &base_induct = base_induct_opt.value();

    if (base_induct.type != InductionVarAnalysis::PlusConst) {
      continue;
    }

    if (!base_induct.consti->is_int() || base_induct.consti->as_int() != 1) {
      continue;
    }

    i128 base_val = cmp_val + (is_leq_type ? 1 : 0);
    TMap<fir::ValueR, i128> values;
    values.insert({base_induct.def, base_val - offset});
    values.insert({induct_candidate, base_val});

    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto &iv : ianal.indirect_inductvars) {
        // skip if already computed
        if (values.contains(iv.def)) {
          continue;
        }

        std::optional<i128> val1;
        std::optional<i128> val2;

        if (auto c1 = get_constant(iv.arg1); c1.has_value()) {
          val1 = c1;
        } else if (auto it = values.find(iv.arg1); it != values.end()) {
          val1 = it->second;
        }

        if (auto c2 = get_constant(iv.arg2); c2.has_value()) {
          val2 = c2;
        } else if (auto it = values.find(iv.arg2); it != values.end()) {
          val2 = it->second;
        }

        if (val1.has_value() && val2.has_value()) {
          i128 result;
          switch (iv.type) {
            case InductionVarAnalysis::PlusConst:
              result = *val1 + *val2;
              break;
            case InductionVarAnalysis::SubConst:
              result = *val1 - *val2;
              break;
            case InductionVarAnalysis::MulConst:
              result = *val1 * *val2;
              break;
            default:
              continue;
          }
          values.insert({iv.def, result});
          changed = true;
        }
      }
    }

    info.push_back(EndInfo{
        .from_bb = lbb.bb,
        .to_bb = out_bb,
        .values = std::move(values),
    });
  }
}

void InductionEndValueAnalysis::dump() {
  fmt::println("Known Ends:");
  for (const auto &i : info) {
    fmt::println("@ {}=>{}:", i.from_bb, i.to_bb);
    for (const auto &[b, v] : i.values) {
      fmt::println("  {}: {}", b, v);
    }
  }
}

void ScalarEvo::update(CFG &cfg, LoopInfo &loop_info) {
  constexpr bool debug_print = false;
  exprs.clear();
  direct_induct.clear();

  auto make_expr = [&](const SCEVExpr &e) -> SCEVExpr::SCEVExprR {
    exprs.push_back(e);
    return exprs.size();
  };

  auto is_loop_invariant = [&](fir::ValueR v) -> bool {
    if (v.is_constant()) {
      return true;
    }
    fir::BasicBlock def_bb{fir::BasicBlock::invalid()};
    if (v.is_instr()) {
      def_bb = v.as_instr()->get_parent();
    } else if (v.is_bb_arg()) {
      def_bb = v.as_bb_arg()->get_parent();
    } else {
      fmt::println("{}", v);
      TODO("Unhandled value type");
    }

    u32 bb_id = cfg.get_bb_id(def_bb);
    return std::ranges::find(loop_info.body_nodes, bb_id) ==
           loop_info.body_nodes.end();
  };

  TMap<fir::ValueR, SCEVExpr::SCEVExprR> lookup;
  TVec<fir::Instr> worklist;
  TSet<fir::ValueR> processed;
  for (auto incoming_arg : cfg.bbrs[loop_info.head].bb->args) {
    lookup.insert(
        {fir::ValueR{incoming_arg},
         make_expr(SCEVExpr{.t = SCEVExpr::Type::Input,
                            .associated_val = fir::ValueR{incoming_arg},
                            .args = {}})});
    processed.insert(fir::ValueR{incoming_arg});
    for (auto u : incoming_arg->uses) {
      worklist.emplace_back(u.user);
    }
  }

  while (!worklist.empty()) {
    auto instr = worklist.back();
    worklist.pop_back();
    if (processed.contains(fir::ValueR{instr})) {
      continue;
    }

    if (instr->is(fir::InstrType::ZExt) || instr->is(fir::InstrType::SExt)) {
      auto is_signext = instr->is(fir::InstrType::SExt);
      SCEVExpr::SCEVExprR lhs = 0;
      SCEVExpr::SCEVExprR rhs = 0;
      if (lookup.contains(instr->args[0])) {
        lhs = lookup.at(instr->args[0]);
      } else if (is_loop_invariant(instr->args[0])) {
        lhs = make_expr(SCEVExpr{.t = SCEVExpr::Type::Invariant,
                                 .associated_val = instr->args[0],
                                 .args = {}});
      } else {
        if (instr->args[0].is_instr()) {
          worklist.push_back(instr->args[0].as_instr());
        }
        continue;
      }
      auto newi = make_expr(SCEVExpr{
          .t = is_signext ? SCEVExpr::Type::SExt : SCEVExpr::Type::ZExt,
          .associated_val = fir::ValueR{instr},
          .args = {lhs, rhs}});
      for (auto u : instr->get_uses()) {
        worklist.push_back(u.user);
      }
      lookup.insert({fir::ValueR{instr}, newi});
      processed.insert(fir::ValueR{instr});
      continue;
    }
    if (instr->is(fir::InstrType::BinaryInstr)) {
      SCEVExpr::SCEVExprR lhs = 0;
      SCEVExpr::SCEVExprR rhs = 0;
      if (lookup.contains(instr->args[0])) {
        lhs = lookup.at(instr->args[0]);
      } else if (is_loop_invariant(instr->args[0])) {
        lhs = make_expr(SCEVExpr{.t = SCEVExpr::Type::Invariant,
                                 .associated_val = instr->args[0],
                                 .args = {}});
      } else {
        if (instr->args[0].is_instr()) {
          worklist.push_back(instr->args[0].as_instr());
        }
        continue;
      }
      if (lookup.contains(instr->args[1])) {
        rhs = lookup.at(instr->args[1]);
      } else if (is_loop_invariant(instr->args[1])) {
        rhs = make_expr(SCEVExpr{.t = SCEVExpr::Type::Invariant,
                                 .associated_val = instr->args[1],
                                 .args = {}});
      } else {
        if (instr->args[1].is_instr()) {
          worklist.push_back(instr->args[1].as_instr());
        }
        continue;
      }

      SCEVExpr::Type ty = SCEVExpr::Type::Invalid;
      switch ((fir::BinaryInstrSubType)instr->subtype) {
        case fir::BinaryInstrSubType::IntAdd:
          ty = SCEVExpr::Type::Add;
          break;
        case fir::BinaryInstrSubType::IntSub:
          ty = SCEVExpr::Type::Sub;
          break;
        case fir::BinaryInstrSubType::IntMul:
          ty = SCEVExpr::Type::Mul;
          break;
        default:
          if constexpr (debug_print) {
            fmt::println("IV2 missed binary {}", instr);
          }
          processed.insert(fir::ValueR{instr});
          continue;
      }

      auto newi = make_expr(SCEVExpr{
          .t = ty, .associated_val = fir::ValueR{instr}, .args = {lhs, rhs}});
      for (auto u : instr->get_uses()) {
        worklist.push_back(u.user);
      }
      lookup.insert({fir::ValueR{instr}, newi});
      processed.insert(fir::ValueR{instr});
      continue;
    }
    if constexpr (debug_print) {
      fmt::println("IV2 missed on {}", instr);
    }
    processed.emplace(instr);
  }

  auto is_direct_induct = [&](this const auto &self, const SCEVExpr &r,
                              fir::BBArgument &arg) -> bool {
    switch (r.t) {
      case SCEVExpr::Type::Add:
      case SCEVExpr::Type::Sub:
      case SCEVExpr::Type::Mul:
        return self(exprs[r.args[0] - 1], arg) &&
               exprs[r.args[1] - 1].t == SCEVExpr::Type::Invariant;
      case SCEVExpr::Type::SExt:
      case SCEVExpr::Type::ZExt:
        return self(exprs[r.args[0] - 1], arg);
      case SCEVExpr::Type::Input:
        return r.associated_val.as_bb_arg() == arg;
      case SCEVExpr::Type::Invalid:
      case SCEVExpr::Type::Invariant:
        return false;
    }
  };

  for (auto t : loop_info.tails) {
    auto term = cfg.bbrs[t].bb->get_terminator();
    auto v = (u32)(std::ranges::find(cfg.bbrs[t].succ, loop_info.head) -
                   cfg.bbrs[t].succ.begin());
    size_t arg_id = 0;
    for (auto a : term->bbs[v].args) {
      if (lookup.contains(a) &&
          is_direct_induct(exprs[lookup.at(a) - 1],
                           cfg.bbrs[loop_info.head].bb->args[arg_id])) {
        direct_induct.emplace_back(arg_id, lookup.at(a));
      }
      arg_id++;
    }
  }
}

void ScalarEvo::dump() const {
  fmt::print("Expr 0: INVALID\n");
  for (size_t i = 0; i < exprs.size(); ++i) {
    fmt::print("Expr {}: ", i + 1);
    const auto &e = exprs[i];
    switch (e.t) {
      case SCEVExpr::Type::Invalid:
        fmt::print("INVALID\n");
        break;
      case SCEVExpr::Type::Input:
        fmt::print("IN {}\n", e.associated_val);
        break;
      case SCEVExpr::Type::Add:
        fmt::print("{} = Add({}, {})\n", e.associated_val, e.args[0],
                   e.args[1]);
        break;
      case SCEVExpr::Type::Sub:
        fmt::print("{} = Sub({}, {})\n", e.associated_val, e.args[0],
                   e.args[1]);
        break;
      case SCEVExpr::Type::ZExt:
        fmt::print("{} = ZExt({})\n", e.associated_val, e.args[0]);
        break;
      case SCEVExpr::Type::SExt:
        fmt::print("{} = SExt({})\n", e.associated_val, e.args[0]);
        break;
      case SCEVExpr::Type::Mul:
        fmt::print("{} = Mul({}, {})\n", e.associated_val, e.args[0],
                   e.args[1]);
        break;
      case SCEVExpr::Type::Invariant:
        fmt::print("Invariant({})\n", e.associated_val);
        break;
    }
  }

  fmt::print("Direct Induction Vars:\n");
  for (auto [arg_id, idx] : direct_induct) {
    fmt::print("  {}: {}\n", arg_id, idx);
  }
  fmt::print("\n");
}

bool LoopBoundsAnalysis::update(ScalarEvo &evo, CFG &cfg, LoopInfo &info) {
  if (evo.direct_induct.empty()) {
    return false;
  }
  if (info.leaving_nodes.size() != 1) {
    return false;
  }
  // can only have 1 incoming value so we can use it as lower bound
  if (cfg.bbrs[info.head].pred.size() != info.tails.size() + 1) {
    return false;
  }
  auto leav_bb = cfg.bbrs[info.leaving_nodes[0]].bb;
  auto leav_term = leav_bb->get_terminator();

  if (!leav_term->is(fir::InstrType::CondBranchInstr)) {
    return false;
  }
  auto loop_continue =
      (u32)(std::ranges::find(cfg.bbrs[info.leaving_nodes[0]].succ, info.head) -
            cfg.bbrs[info.leaving_nodes[0]].succ.begin());

  auto cond = leav_term->args[0];
  if (!cond.is_instr()) {
    return false;
  }
  auto condi = cond.as_instr();
  auto sub = (fir::ICmpInstrSubType)condi->subtype;
  bool is_eql_cond =
      sub == fir::ICmpInstrSubType::NE || sub == fir::ICmpInstrSubType::EQ;
  bool isle =
      sub == fir::ICmpInstrSubType::SLE || sub == fir::ICmpInstrSubType::ULE;

  if (!condi->is(fir::InstrType::ICmp) ||
      (!is_eql_cond && sub != fir::ICmpInstrSubType::SLT &&
       sub != fir::ICmpInstrSubType::ULT && !isle)) {
    return false;
  }
  if (!condi->args[1].is_constant()) {
    return false;
  }
  auto condi_const = condi->args[1].as_constant();
  i128 condi_const_val = 0;
  if (condi_const->is_int()) {
    condi_const_val = condi_const->as_int();
  } else if (condi_const->is_null()) {
    condi_const_val = 0;
  } else {
    return false;
  }
  end_value = condi_const_val + (isle ? 1 : 0);
  auto induct_id =
      std::ranges::find_if(evo.direct_induct, [&](const auto &a) -> bool {
        const SCEVExpr &expr = evo.exprs[a.second - 1];
        return expr.associated_val == condi->args[0];
      });
  // TODO: this isnt very robust we might be using teh value oft he previous
  // iteration as a condition
  //  which then would fail this check
  //  maybe implement a forwarding system??
  if (induct_id == evo.direct_induct.end()) {
    return false;
  }
  induct = induct_id->second;
  auto &expr = evo.exprs[induct_id->second - 1];
  if (expr.t != SCEVExpr::Type::Add ||
      // TODO support other stuff like sub atleast
      !evo.exprs[expr.args[1] - 1].associated_val.is_constant()) {
    return false;
  }
  change_val =
      evo.exprs[expr.args[1] - 1].associated_val.as_constant()->as_int();

  // find out which incoming edge is the from outside the loop
  // and then use its values if they are constant
  u32 incoming_bb = 0;
  for (auto incoming : cfg.bbrs[info.head].pred) {
    if (std::ranges::find(info.body_nodes, incoming) == info.body_nodes.end()) {
      incoming_bb = incoming;
      break;
    }
  }
  auto incoming_term = cfg.bbrs[incoming_bb].bb->get_terminator();
  // TODO: support cbranch/switch
  if (!incoming_term->is(fir::InstrType::BranchInstr)) {
    return false;
  }
  auto lowwer_boundv = incoming_term->bbs[0].args[induct_id->first];
  if (!lowwer_boundv.is_constant()) {
    return false;
  }
  start_value = lowwer_boundv.as_constant()->as_int();

  ASSERT(change_val > 0);
  if (is_eql_cond) {
    if (loop_continue == 0) {
      // means if the targets of the leaving cbranch are switched the condition
      // is effectively also switched
      return false;
    }
    if ((change_val > 0 && start_value > end_value) ||
        (change_val < 0 && start_value < end_value)) {
      // it might overflow??
      return false;
    }
    real_end_value = end_value;
    i128 range = (end_value - start_value);
    if (range % change_val != 0) {
      // this would just cause garbage problems with overflow and shit right??
      return false;
      // real_end_value = end_value + (change_val - (range % change_val));
    }
    n_iter = (real_end_value - start_value) / change_val;
  } else {
    if (loop_continue != 0) {
      // means if the targets of the leaving cbranch are switched the condition
      // is effectively also switched
      TODO("impl different continue");
      // return false;
    }
    if ((change_val > 0 && start_value > end_value) ||
        (change_val < 0 && start_value < end_value)) {
      // it might overflow??
      return false;
    }

    // if the upper bound is not divisible by teh incr the final value of the
    // induction variable will be higher and not equal to the upper bound
    // the exact value depends on the starting value + upper_bound mod  incr
    real_end_value = end_value;
    i128 range = (end_value - start_value);
    if (range % change_val != 0) {
      real_end_value = end_value + (change_val - (range % change_val));
    }
    n_iter = (real_end_value - start_value) / change_val;
  }
  return true;
}

void LoopBoundsAnalysis::dump() const {
  fmt::println("LoopsBoundAnalysis:");
  fmt::println("  InductId:{}", induct);
  fmt::println("  Iter:{}", n_iter);
  fmt::println("  Start:{}", start_value);
  fmt::println("  Change:{}", change_val);
  fmt::println("  End:{}({})", end_value, real_end_value);
}

}  // namespace foptim::optim
