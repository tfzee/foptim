#include "constraint_analysis.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include <fmt/base.h>
#include <optional>

namespace foptim::optim {

bool ConstraintAnalysis::get_bin_expr(fir::Instr i, u32 depth) {
  auto node_type = ExprNode::INVALID;
  if (i->is(fir::BinaryInstrSubType::IntSub)) {
    node_type = ExprNode::Sub;
  }
  if (node_type != ExprNode::INVALID) {
    auto v1 = get_expr(i->args[0], depth + 1);
    auto v2 = get_expr(i->args[1], depth + 1);
    exprs.push_back({node_type, fir::ValueR{i}, v1, v2});
    return true;
  }
  return false;
}

ConstraintAnalysis::ExprId ConstraintAnalysis::get_expr(fir::ValueR v,
                                                        u32 depth) {
  if (const auto it = expr_ids.find(v); it != expr_ids.end()) {
    return it->second;
  }
  if (depth > max_expr_depth) {
    exprs.push_back({ExprNode::ExprNodeType::InpVal, v});
    return exprs.size();
  }
  if (v.is_instr()) {
    auto i = v.as_instr();
    // TODO: for now onlz depth 1
    if (i->is(fir::ConversionSubType::PtrToInt) ||
        i->is(fir::ConversionSubType::IntToPtr)) {
      return get_expr(i->args[0], depth);
    }

    if (get_bin_expr(i, depth)) {
    } else {
      exprs.push_back({ExprNode::ExprNodeType::InpVal, v});
    }
  } else if (v.is_constant()) {
    exprs.push_back({ExprNode::ExprNodeType::InpConst, v});
  } else {
    exprs.push_back({ExprNode::ExprNodeType::InpVal, v});
  }
  // the node for v is always the last one pushed (children come first)
  expr_ids.emplace(v, exprs.size());
  return exprs.size();
}

std::optional<ConstraintAnalysis::ConstrId>
ConstraintAnalysis::get_constraint(fir::Instr cond) {
  auto cond_res = Constraint::INVALID;
  // auto inv_cond_res = Constraint::INVALID;
  if (cond->is(fir::ICmpInstrSubType::EQ)) {
    cond_res = Constraint::ConstraintType::EQ;
    // inv_cond_res = Constraint::ConstraintType::NE;
  } else if (cond->is(fir::ICmpInstrSubType::NE)) {
    cond_res = Constraint::ConstraintType::NE;
    // inv_cond_res = Constraint::ConstraintType::EQ;
  } else if (cond->is(fir::ICmpInstrSubType::ULT)) {
    cond_res = Constraint::ConstraintType::ULT;
    // inv_cond_res = Constraint::ConstraintType::UGE;
  } else if (cond->is(fir::ICmpInstrSubType::SLT)) {
    cond_res = Constraint::ConstraintType::SLT;
    // inv_cond_res = Constraint::ConstraintType::SGE;
  } else if (cond->is(fir::ICmpInstrSubType::ULE)) {
    cond_res = Constraint::ConstraintType::ULE;
    // inv_cond_res = Constraint::ConstraintType::UGT;
  } else if (cond->is(fir::ICmpInstrSubType::SLE)) {
    cond_res = Constraint::ConstraintType::SLE;
    // inv_cond_res = Constraint::ConstraintType::SGT;
  } else if (cond->is(fir::ICmpInstrSubType::UGT)) {
    cond_res = Constraint::ConstraintType::UGT;
    // inv_cond_res = Constraint::ConstraintType::ULE;
  } else if (cond->is(fir::ICmpInstrSubType::SGT)) {
    cond_res = Constraint::ConstraintType::SGT;
    // inv_cond_res = Constraint::ConstraintType::SLE;
  } else if (cond->is(fir::ICmpInstrSubType::UGE)) {
    cond_res = Constraint::ConstraintType::UGE;
    // inv_cond_res = Constraint::ConstraintType::ULT;
  } else if (cond->is(fir::ICmpInstrSubType::SGE)) {
    cond_res = Constraint::ConstraintType::SGE;
    // inv_cond_res = Constraint::ConstraintType::SLT;
  }
  if (cond_res != Constraint::INVALID) {
    ExprId expr1 = get_expr(cond->args[0]);
    ExprId expr2 = get_expr(cond->args[1]);
    return get_constraint(cond_res, expr1, expr2, cond);
  }
  return std::nullopt;
}

ConstraintAnalysis::ConstrId
ConstraintAnalysis::get_constraint(Constraint::ConstraintType ty, ExprId v1,
                                   ExprId v2, fir::Instr origin_instr) {

  const ConstrKey key{ty, v1, v2, origin_instr};
  if (const auto it = constraint_ids.find(key); it != constraint_ids.end()) {
    return it->second;
  }
  constraints.push_back({ty, v1, v2, origin_instr});
  constraint_ids.emplace(key, constraints.size());
  return constraints.size();
}

void ConstraintAnalysis::propagate_constraints() {
  TVec<u32> worklist;
  for (u32 bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    if (bb_to_constraints[bb_id].active_constraints.empty()) {
      continue;
    }
    worklist.push_back(bb_id);
  }

  while (!worklist.empty()) {
    auto c_id = worklist.back();
    worklist.pop_back();
    if (bb_to_constraints[c_id].active_constraints.empty()) {
      continue;
    }
    auto &cur = cfg.bbrs[c_id];
    if (cur.succ.empty()) {
      continue;
    }
    if (cur.pred.size() == 1) {
      // TODO: cause ssa constraint gotta be true here aswell but idk if useful
    }
    for (auto succ : cur.succ) {
      if (cfg.bbrs[succ].pred.size() != 1) {
        // implement merging
        continue;
      }
      bb_to_constraints[succ].active_constraints.insert(
          bb_to_constraints[c_id].active_constraints.begin(),
          bb_to_constraints[c_id].active_constraints.end());
    }
  }
}

void ConstraintAnalysis::update() {
  ZoneScopedNC("CONSTRAINT UPDATE", COLOR_ANALY);
  reset_and_resize();
  setup_direct_constraints();
  setup_inferred_constraints();
  propagate_constraints();
  // dump();
}

void ConstraintAnalysis::reset_and_resize() {
  exprs.clear();
  constraints.clear();
  expr_ids.clear();
  constraint_ids.clear();
  bb_to_constraints.clear();

  bb_to_constraints.resize(cfg.bbrs.size(), {});
}

void ConstraintAnalysis::infer_simplificaitons(ConstrId c,
                                               TVec<ConstrId> &new_constr) {
  auto get_expr = [this](ExprId i) {
    ASSERT(i != 0);
    return exprs[i - 1];
  };
  // auto is_constant = [this](ExprId i) {
  //   ASSERT(i != 0);
  //   return exprs[i - 1].type == ExprNode::InpConst;
  // };
  auto is_sub = [this](ExprId i) {
    ASSERT(i != 0);
    return exprs[i - 1].type == ExprNode::Sub;
  };
  auto is_int_greater = [this](ExprId i, i128 c) {
    ASSERT(i != 0);
    auto v = exprs[i - 1];
    return v.type == ExprNode::InpConst && v.v.is_constant_int() &&
           v.v.as_constant()->as_int() > c;
  };

  auto &con = constraints[c - 1];
  // (a-b) > c (if c > 0) ==> a != b
  if ((con.type == Constraint::UGT || con.type == Constraint::SGT ||
       con.type == Constraint::UGE || con.type == Constraint::SGE) &&
      is_sub(con.e1) && is_int_greater(con.e2, 0)) {
    auto sub = get_expr(con.e1);
    new_constr.push_back(
        get_constraint(Constraint::NE, sub.i1, sub.i2, con.origin_instr));
  }
}

void ConstraintAnalysis::setup_inferred_constraints() {
  TVec<ConstrId> add_constraints;
  for (auto &bb : bb_to_constraints) {
    add_constraints.clear();
    for (auto constr : bb.active_constraints) {
      infer_simplificaitons(constr, add_constraints);
    }
    for (auto add_constr : add_constraints) {
      // TODO: prob want to do duplicate check here or earlier already
      bb.active_constraints.insert(add_constr);
    }
  }
}

void ConstraintAnalysis::setup_direct_constraints() {
  for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    auto &bbr = cfg.bbrs[bb_id];
    auto term = bbr.bb->get_terminator();
    // TODO: add switch statement
    if (!term->is(fir::InstrType::CondBranchInstr)) {
      continue;
    }
    if (!term->args[0].is_instr()) {
      continue;
    }
    auto cond = term->args[0].as_instr();

    auto constraint = get_constraint(cond);
    if (constraint.has_value()) {
      auto true_target = cfg.get_bb_id(term->bbs[0].bb);
      auto false_target = cfg.get_bb_id(term->bbs[1].bb);
      bb_to_constraints[bb_id].terminator_constraint = constraint.value();
      if (cfg.bbrs[true_target].pred.size() > 1) {
        bb_to_constraints[true_target].active_constraints.insert(
            constraint.value());
      }
      if (cfg.bbrs[false_target].pred.size() == 1) {
        auto inv_cond_res = Constraint::INVALID;
        auto &constrs = constraints[constraint.value() - 1];
        if (constrs.type == Constraint::ConstraintType::EQ) {
          inv_cond_res = Constraint::ConstraintType::NE;
        } else if (constrs.type == Constraint::ConstraintType::NE) {
          inv_cond_res = Constraint::ConstraintType::EQ;
        } else if (constrs.type == Constraint::ConstraintType::ULT) {
          inv_cond_res = Constraint::ConstraintType::UGE;
        } else if (constrs.type == Constraint::ConstraintType::SLT) {
          inv_cond_res = Constraint::ConstraintType::SGE;
        } else if (constrs.type == Constraint::ConstraintType::ULE) {
          inv_cond_res = Constraint::ConstraintType::UGT;
        } else if (constrs.type == Constraint::ConstraintType::SLE) {
          inv_cond_res = Constraint::ConstraintType::SGT;
        } else if (constrs.type == Constraint::ConstraintType::UGT) {
          inv_cond_res = Constraint::ConstraintType::ULE;
        } else if (constrs.type == Constraint::ConstraintType::SGT) {
          inv_cond_res = Constraint::ConstraintType::SLE;
        } else if (constrs.type == Constraint::ConstraintType::UGE) {
          inv_cond_res = Constraint::ConstraintType::ULT;
        } else if (constrs.type == Constraint::ConstraintType::SGE) {
          inv_cond_res = Constraint::ConstraintType::SLT;
        }
        bb_to_constraints[false_target].active_constraints.insert(
            get_constraint(inv_cond_res, constrs.e1, constrs.e2, cond));
      }
    }
  }
}

void ConstraintAnalysis::dump() {
  for (size_t bb_id = 0; bb_id < bb_to_constraints.size(); bb_id++) {
    auto &bb = bb_to_constraints[bb_id];
    if (bb.terminator_constraint != 0) {
      fmt::println("BB {}:                   TERMINATES ON {:cd}", bb_id,
                   printConstr(bb.terminator_constraint));
    } else {
      fmt::println("BB {}:", bb_id);
    }
    for (auto &c : bb.active_constraints) {
      fmt::println(
          "   {:cd}            from BB:{}", printConstr(c),
          cfg.get_bb_id(constraints[c - 1].origin_instr->get_parent()));
    }
  }
}

bool ConstraintAnalysis::contradicts(ConstrId x, ConstrId y) {
  // NOTE: CLAUDE AI GENERATED FUNCTION
  auto &cx = constraints[x - 1];
  auto &cy = constraints[y - 1];

  // Only handle constraints over the same pair of expressions
  bool same_order = (cx.e1 == cy.e1 && cx.e2 == cy.e2);
  bool swapped = (cx.e1 == cy.e2 && cx.e2 == cy.e1);
  if (!same_order && !swapped) {
    return false;
  }

  auto tx = cx.type;
  auto ty = cy.type;

  // Normalize: if swapped, flip the relational direction of ty so that
  // both constraints are expressed over (cx.e1, cx.e2).
  auto flip = [](Constraint::ConstraintType t) {
    switch (t) {
    case Constraint::EQ:
      return Constraint::EQ;
    case Constraint::NE:
      return Constraint::NE;
    case Constraint::UGT:
      return Constraint::ULT;
    case Constraint::UGE:
      return Constraint::ULE;
    case Constraint::ULT:
      return Constraint::UGT;
    case Constraint::ULE:
      return Constraint::UGE;
    case Constraint::SGT:
      return Constraint::SLT;
    case Constraint::SGE:
      return Constraint::SLE;
    case Constraint::SLT:
      return Constraint::SGT;
    case Constraint::SLE:
      return Constraint::SGE;
    default:
      return Constraint::INVALID;
    }
  };
  if (swapped) {
    ty = flip(ty);
  }

  if (tx == Constraint::INVALID || ty == Constraint::INVALID) {
    return false;
  }

  // Don't cross-compare signed vs unsigned relational ops - we don't know
  // the sign relationship in general, so only EQ/NE combine safely with
  // either signedness. Restrict strict relational contradictions to same
  // signedness family (or EQ/NE, which are signedness-agnostic).
  auto is_unsigned_rel = [](Constraint::ConstraintType t) {
    return t == Constraint::UGT || t == Constraint::UGE ||
           t == Constraint::ULT || t == Constraint::ULE;
  };
  auto is_signed_rel = [](Constraint::ConstraintType t) {
    return t == Constraint::SGT || t == Constraint::SGE ||
           t == Constraint::SLT || t == Constraint::SLE;
  };
  bool mixed_signedness = (is_unsigned_rel(tx) && is_signed_rel(ty)) ||
                          (is_signed_rel(tx) && is_unsigned_rel(ty));
  if (mixed_signedness) {
    return false;
  }

  // EQ vs everything
  if (tx == Constraint::EQ) {
    switch (ty) {
    case Constraint::NE:
    case Constraint::UGT:
    case Constraint::ULT:
    case Constraint::SGT:
    case Constraint::SLT:
      return true; // EQ contradicts strict inequality/NE
    default:
      return false; // EQ vs GE/LE/EQ is consistent
    }
  }
  if (ty == Constraint::EQ) {
    switch (tx) {
    case Constraint::NE:
    case Constraint::UGT:
    case Constraint::ULT:
    case Constraint::SGT:
    case Constraint::SLT:
      return true;
    default:
      return false;
    }
  }

  // NE vs NE, or NE vs relational (NE alone never contradicts a
  // non-strict/strict inequality by itself)
  if (tx == Constraint::NE || ty == Constraint::NE) {
    return false;
  }

  // Now both are relational (>,>=,<,<=) of the same signedness family.
  // Map to a common "greater" orientation: GT=2, GE=1, LT=-2, LE=-1
  auto rank = [](Constraint::ConstraintType t) -> int {
    switch (t) {
    case Constraint::UGT:
    case Constraint::SGT:
      return 2;
    case Constraint::UGE:
    case Constraint::SGE:
      return 1;
    case Constraint::ULE:
    case Constraint::SLE:
      return -1;
    case Constraint::ULT:
    case Constraint::SLT:
      return -2;
    default:
      return 0;
    }
  };

  int rx = rank(tx);
  int ry = rank(ty);
  if (rx == 0 || ry == 0) {
    return false;
  }

  // rx, ry describe relation of e1 vs e2 (>, >=, <, <=).
  // Contradiction happens when the signs disagree and at least one is strict,
  // or they disagree entirely (one says >, other says <=, etc.)
  bool x_pos = rx > 0; // e1 > e2 in some sense
  bool y_pos = ry > 0;

  if (x_pos == y_pos) {
    return false; // same direction, can't contradict (e.g. GT & GE fine)
  }

  // Opposite directions: e.g. x says e1 > e2 (or >=), y says e1 < e2 (or <=)
  // These only fail to contradict if both are non-strict AND could both be
  // equality (GE and LE are consistent, both satisfied when e1 == e2)
  bool x_strict = (rx == 2 || rx == -2);
  bool y_strict = (ry == 2 || ry == -2);
  return x_strict || y_strict;
}

bool ConstraintAnalysis::contradicts(ConstrId v, TSet<ConstrId> &orig) {
  // if were in the set we cant contradict unless the set was already
  // contradicting itself
  for (auto x : orig) {
    if (x == v) {
      return false;
    }
  }
  for (auto x : orig) {
    if (contradicts(v, x)) {
      return true;
    }
  }

  return false;
}

} // namespace foptim::optim
