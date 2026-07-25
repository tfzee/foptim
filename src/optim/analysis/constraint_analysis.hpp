#pragma once
#include <fmt/base.h>
#include <fmt/format.h>

#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/dominators.hpp"

namespace foptim::optim {

// Collect per bb constraints that are active in this bb.
class ConstraintAnalysis {
public:
  using ExprId = uint32_t;
  using ConstrId = uint32_t;

  struct ExprNode {
    enum ExprNodeType {
      INVALID,
      InpVal,
      InpConst,
      Add,
      Sub,
      And,
      Or,
      Xor,
      // TODO more ops
    };

    ExprNodeType type = INVALID;
    fir::ValueR v;
    ExprId i1 = 0;
    ExprId i2 = 0;
  };
  struct Constraint {
    enum ConstraintType {
      INVALID,
      EQ,
      NE,
      UGT,
      UGE,
      ULT,
      ULE,
      SGT,
      SGE,
      SLT,
      SLE,
      // TODO floating point ordered/unordered
    };
    ConstraintType type = INVALID;
    ExprId e1 = 0;
    ExprId e2 = 0;
    fir::Instr origin_instr;
  };

  struct BBConstraintData {
    ConstrId terminator_constraint = 0;
    TVec<ConstrId> active_constraints;
  };

  TVec<ExprNode> exprs;
  TVec<Constraint> constraints;
  TVec<BBConstraintData> bb_to_constraints;
  u32 max_expr_depth = 1;

  CFG &cfg;
  Dominators &dom;

  using PrintWrapperExpr = PrintWrapper<ExprNode &, ConstraintAnalysis *>;
  using PrintWrapperConstr = PrintWrapper<Constraint &, ConstraintAnalysis *>;

  PrintWrapperExpr printExpr(ExprId id) {
    ASSERT(id > 0);
    return {.data = exprs[id - 1], .ctx = this};
  }

  PrintWrapperConstr printConstr(ConstrId id) {
    ASSERT(id > 0);
    return {.data = constraints[id - 1], .ctx = this};
  }

private:
  bool get_bin_expr(fir::Instr i, u32 depth);

  ExprId get_expr(fir::ValueR v, u32 depth = 0);

  ConstrId get_constraint(Constraint::ConstraintType ty, ExprId v1, ExprId v2,
                          fir::Instr origin_instr);
  void infer_simplificaitons(ConstrId c, TVec<ConstrId> &new_constr);

public:
  std::optional<ConstrId> get_constraint(fir::Instr i);
  ConstraintAnalysis(CFG &cfg, Dominators &dom) : cfg(cfg), dom(dom) {
    update();
  }

  void dump();
  void update();
  void reset_and_resize();
  void setup_direct_constraints();
  void setup_inferred_constraints();
  // returns true if we are sure that v will contradict one or multiple of the
  // constraints in orig. So it might be true but it might not return true
  // However there are no false positives as such if there are no contradiction
  // it will *never* return true
  // Iff the orig input is already contradicting itself then the output is
  // unspecificed
  bool contradicts(ConstrId v, TVec<ConstrId> &orig);
  bool contradicts(ConstrId x, ConstrId y);
  // return true if the second input supports the first
  // so only returns true if the first can be inferred from the second
  // this might yield false negatives but never false positives.
  bool supported(ConstrId v, TVec<ConstrId> &orig);
  bool supported(ConstrId x, ConstrId sup);
};

} // namespace foptim::optim

template <>
class fmt::formatter<foptim::optim::ConstraintAnalysis::PrintWrapperExpr>
    : public BaseIRFormatter<
          foptim::optim::ConstraintAnalysis::PrintWrapperExpr> {
public:
  appender
  format(foptim::optim::ConstraintAnalysis::PrintWrapperExpr const &data,
         format_context &ctx) const {
    // using Constraint = foptim::optim::ConstraintAnalysis::Constraint;
    using ExprNode = foptim::optim::ConstraintAnalysis::ExprNode;
    auto app = ctx.out();
    switch (data.data.type) {
    case ExprNode::INVALID:
      app = fmt::format_to(app, "INVALID");
      break;
    case ExprNode::Sub:
      if (color) {
        app = fmt::format_to(app, "({:c} - {:c})",
                             data.ctx->printExpr(data.data.i1),
                             data.ctx->printExpr(data.data.i2));
      } else {
        app =
            fmt::format_to(app, "({} - {})", data.ctx->printExpr(data.data.i1),
                           data.ctx->printExpr(data.data.i2));
      }
      break;
    case ExprNode::Add:
    case ExprNode::And:
    case ExprNode::Or:
    case ExprNode::Xor:
      if (color) {
        app = fmt::format_to(app, "({:c} OP {:c})",
                             data.ctx->printExpr(data.data.i1),
                             data.ctx->printExpr(data.data.i2));
      } else {
        app =
            fmt::format_to(app, "({} OP {})", data.ctx->printExpr(data.data.i1),
                           data.ctx->printExpr(data.data.i2));
      }
      break;
    case ExprNode::InpVal:
    case ExprNode::InpConst:
      if (color) {
        app = fmt::format_to(app, "{:c}", data.data.v);
      } else {
        app = fmt::format_to(app, "{}", data.data.v);
      }
      break;
    }
    return app;
  }
};

template <>
class fmt::formatter<foptim::optim::ConstraintAnalysis::PrintWrapperConstr>
    : public BaseIRFormatter<
          foptim::optim::ConstraintAnalysis::PrintWrapperConstr> {
public:
  appender
  format(foptim::optim::ConstraintAnalysis::PrintWrapperConstr const &data,
         format_context &ctx) const {
    using Constraint = foptim::optim::ConstraintAnalysis::Constraint;
    // using ExprNode = foptim::optim::ConstraintAnalysis::ExprNode;
    auto app = ctx.out();
    if (color) {
      app = fmt::format_to(app, "{:c}", data.ctx->printExpr(data.data.e1));
    } else {
      app = fmt::format_to(app, "{}", data.ctx->printExpr(data.data.e1));
    }
    switch (data.data.type) {
    case Constraint::INVALID:
      app = fmt::format_to(app, color_func, "INVALID");
      break;
    case Constraint::EQ:
      app = fmt::format_to(app, color_func, " == ");
      break;
    case Constraint::NE:
      app = fmt::format_to(app, color_func, " != ");
      break;
    case Constraint::SGT:
      app = fmt::format_to(app, color_func, " >S ");
      break;
    case Constraint::SGE:
      app = fmt::format_to(app, color_func, " >S= ");
      break;
    case Constraint::SLT:
      app = fmt::format_to(app, color_func, " <S ");
      break;
    case Constraint::SLE:
      app = fmt::format_to(app, color_func, " <S= ");
      break;
    case Constraint::UGT:
      app = fmt::format_to(app, color_func, " >U ");
      break;
    case Constraint::UGE:
      app = fmt::format_to(app, color_func, " >U= ");
      break;
    case Constraint::ULT:
      app = fmt::format_to(app, color_func, " <U ");
      break;
    case Constraint::ULE:
      app = fmt::format_to(app, color_func, " <U= ");
      break;
    }
    if (color) {
      app = fmt::format_to(app, "{:c}", data.ctx->printExpr(data.data.e2));
    } else {
      app = fmt::format_to(app, "{}", data.ctx->printExpr(data.data.e2));
    }
    return app;
  }
};
