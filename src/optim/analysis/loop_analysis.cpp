#include "loop_analysis.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "utils/bitset.hpp"
#include "utils/set.hpp"
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
  return v.is_constant() ? std::optional{i128(v.as_constant()->as_int())}
                         : std::nullopt;
}

std::optional<InductionVarAnalysis::InductionVar>
resolve_base_induction(const fir::ValueR &v, const InductionVarAnalysis &ianal,
                       i128 &acc_offset) {
  auto current = std::ranges::find_if(
      ianal.indirect_inductvars, [&](const auto &iv) { return iv.def == v; });

  if (current != ianal.indirect_inductvars.end() ||
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

  auto base =
      std::ranges::find_if(ianal.direct_inductvars, [&](const auto &dv) {
        return dv.def == current->arg1;
      });

  if (base != ianal.direct_inductvars.end()) {
    return *base;
  }

  return std::nullopt;
}
} // namespace

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
    values.insert({base_induct.def, base_val});
    values.insert({induct_candidate, base_val + offset});

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

// void InductionEndValueAnalysis::update(CFG &cfg, LoopInfo &linfo,
//                                        InductionVarAnalysis &ianal) {
//   info.clear();
//   TSet<fir::BasicBlock> loop_bbs{};
//   loop_bbs.reserve(linfo.body_nodes.size());
//   for (auto v : linfo.body_nodes) {
//     loop_bbs.insert(cfg.bbrs[v].bb);
//   }
//   // just doing 1 leaving node for now
//   for (auto leav : linfo.leaving_nodes) {
//     auto &lbb = cfg.bbrs[leav];
//     if (lbb.succ.size() != 2) {
//       continue;
//     }
//     auto term = lbb.bb->get_terminator();
//     // TODO: make it handle both
//     if (loop_bbs.contains(term->bbs[1].bb)) {
//       continue;
//     }
//     auto condv = term->args[0];
//     if (!condv.is_instr()) {
//       continue;
//     }
//     auto cond = condv.as_instr();
//     if (!cond->is(fir::InstrType::ICmp)) {
//       continue;
//     }
//     bool cond_on_equal = cond->subtype == (u32)fir::ICmpInstrSubType::ULE ||
//                          cond->subtype == (u32)fir::ICmpInstrSubType::SLE;
//     if (!cond_on_equal && cond->subtype != (u32)fir::ICmpInstrSubType::SLT &&
//         cond->subtype != (u32)fir::ICmpInstrSubType::ULT) {
//       continue;
//     }
//     // arg1 cannot be modified in the loop
//     // TODO: technically we could haveo thervals here aswell
//     if (!cond->args[1].is_constant()) {
//       continue;
//     }
//     auto condition_val = cond->args[1].as_constant();
//     auto induct_used_iter =
//         std::ranges::find_if(ianal.indirect_inductvars, [&](const auto &a) {
//           return a.def == cond->args[0];
//         });
//     if (induct_used_iter == ianal.indirect_inductvars.end()) {
//       continue;
//     }
//     auto &induct_used = *induct_used_iter;
//     if (induct_used.type != InductionVarAnalysis::PlusConst ||
//         !induct_used.arg2.is_constant()) {
//       continue;
//     }

//     i128 offset_of_base_induct_var = 0;
//     auto parent_var = induct_used_iter;
//     while (true) {
//       offset_of_base_induct_var += parent_var->arg2.as_constant()->as_int();
//       auto new_var = std::ranges::find_if(
//           ianal.indirect_inductvars,
//           [&](const InductionVarAnalysis::IInductionVar &v) {
//             return v.type == InductionVarAnalysis::PlusConst &&
//                    v.def == parent_var->arg1;
//           });
//       if (new_var == ianal.indirect_inductvars.end()) {
//         break;
//       }
//       parent_var = new_var;
//     }
//     auto base_induct_var =
//         std::ranges::find_if(ianal.direct_inductvars,
//                              [&](const InductionVarAnalysis::InductionVar &v)
//                              {
//                                return v.def == parent_var->arg1;
//                              });
//     if (base_induct_var == ianal.direct_inductvars.end() ||
//         base_induct_var->type != InductionVarAnalysis::PlusConst ||
//         // for checking != 1 we could also check if we know start value and
//         end
//         // value and step value it should be end+(end - start)%step
//         base_induct_var->consti->as_int() != 1) {
//       continue;
//     }

//     auto base_value = condition_val->as_int() + (cond_on_equal ? 1 : 0);
//     TMap<fir::ValueR, i128> known_values;
//     known_values.insert({base_induct_var->def, base_value});
//     known_values.insert(
//         {induct_used.def, base_value + offset_of_base_induct_var});

//     TMap<fir::ValueR, i128> values;
//     values.insert({induct_used.def, condition_val->as_int()});
//     values.insert({base_induct_var->def,
//                    condition_val->as_int() - offset_of_base_induct_var});

//     info.push_back(EndInfo{
//         .from_bb = lbb.bb,
//         .to_bb = term->bbs[1].bb,
//         .values = values,
//     });

//     // fmt::println("{}", base_induct_var->def);
//     // fmt::println("{}", offset_of_base_induct_var);
//     // fmt::println("{}", lbb.bb);
//   }
// }

void InductionEndValueAnalysis::dump() {
  fmt::println("Known Ends:");
  for (const auto &i : info) {
    fmt::println("@ {}=>{}:", i.from_bb, i.to_bb);
    for (const auto &[b, v] : i.values) {
      fmt::println("  {}: {}", b, v);
    }
  }
}
} // namespace foptim::optim
