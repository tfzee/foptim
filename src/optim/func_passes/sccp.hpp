#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include "utils/set.hpp"
#include <algorithm>

namespace foptim::optim {

class SCCP final : public FunctionPass {

  struct SSAEdge {
    // DumbName
    fir::Instr origin;
    fir::Use use;
  };

  struct ConstantValue {
    enum class ValueType {
      Top,
      Constant,
      Bottom,
    };

    ValueType type;
    fir::ConstantValueR value =
        fir::ConstantValueR(fir::ConstantValueR::invalid());

    [[nodiscard]] constexpr bool is_top() const {
      return type == ValueType::Top;
    }
    [[nodiscard]] constexpr bool is_bottom() const {
      return type == ValueType::Bottom;
    }
    [[nodiscard]] constexpr bool is_const() const {
      return type == ValueType::Constant;
    }

    static ConstantValue Top() {
      return ConstantValue{ValueType::Top,
                           fir::ConstantValueR(fir::ConstantValueR::invalid())};
    }
    static ConstantValue Bottom() {
      return ConstantValue{ValueType::Bottom,
                           fir::ConstantValueR(fir::ConstantValueR::invalid())};
    }
    static ConstantValue Constant(fir::ConstantValueR v) {
      return ConstantValue{ValueType::Constant, v};
    }

    bool operator==(const ConstantValue &other) const {
      if (type != other.type) {
        return false;
      }
      if (type == ValueType::Constant) {
        return value->eql(*other.value.operator->());
      }
      return true;
    }
  };

  CFG cfg;
  // TEMPORARY STORAGE
  std::deque<fir::Use, utils::TempAlloc<fir::Use>> ssa_worklist;
  std::deque<fir::BasicBlock, utils::TempAlloc<fir::BasicBlock>> cfg_worklist;

  TMap<fir::ValueR, ConstantValue> values;

  TSet<fir::BasicBlock> reachable_bb;
  TSet<fir::BasicBlock> bottom_bbs;

public:
  ConstantValue eval(fir::ValueR value) {
    if (value.is_constant()) {
      return ConstantValue::Constant(value.as_constant());
    }
    if (values.contains(value)) {
      return values.at(value);
    }

    return ConstantValue::Bottom();
  }

  ConstantValue eval_instr(fir::Context &ctx, fir::Instr instr) {

    switch (instr->get_instr_type()) {
    case fir::InstrType::BinaryInstr: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));

      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() || !b.is_const()) {
        return ConstantValue::Top();
      }

      if ((!a.value->is_int() && !a.value->is_float()) ||
          (!b.value->is_int() && !b.value->is_float())) {
        failure(
            {"Cannot do SCCP on binary expr using non integers/floats", instr});
        return ConstantValue::Bottom();
      }

      auto out_type = a.value->get_type();

      switch ((fir::BinaryInstrSubType)instr->get_instr_subtype()) {
      default:
        utils::Debug << instr << "\n";
        IMPL("implement instr");
        UNREACH();
      case fir::BinaryInstrSubType::INVALID:
        UNREACH();
      case fir::BinaryInstrSubType::Shl:
        return ConstantValue::Constant(
            ctx->get_constant_value((a.value->as_int() << b.value->as_int()) &
                                        ((2 << out_type->as_int()) - 1),
                                    out_type));
      case fir::BinaryInstrSubType::IntSub:
        return ConstantValue::Constant(ctx->get_constant_value(
            a.value->as_int() - b.value->as_int(), out_type));
      case fir::BinaryInstrSubType::IntAdd:
        return ConstantValue::Constant(ctx->get_constant_value(
            a.value->as_int() + b.value->as_int(), out_type));
      case fir::BinaryInstrSubType::IntSRem:
        return ConstantValue::Constant(ctx->get_constant_value(
            ((i64)a.value->as_int()) % ((i64)b.value->as_int()), out_type));
      case fir::BinaryInstrSubType::IntMul:
        return ConstantValue::Constant(ctx->get_constant_value(
            a.value->as_int() * b.value->as_int(), out_type));
      case fir::BinaryInstrSubType::FloatAdd:
        return ConstantValue::Constant(ctx->get_constant_value(
            a.value->as_float() + b.value->as_float(), out_type));
      case fir::BinaryInstrSubType::FloatMul:
        return ConstantValue::Constant(ctx->get_constant_value(
            a.value->as_float() * b.value->as_float(), out_type));
      case fir::BinaryInstrSubType::FloatSub:
        return ConstantValue::Constant(ctx->get_constant_value(
            a.value->as_float() - b.value->as_float(), out_type));
      }
      UNREACH();
    }
    case fir::InstrType::BranchInstr: {
      const auto &target = instr->get_bb_args();
      // utils::Debug << " HIT BRANCH\n\n";
      ASSERT(target.size() == 1);
      const auto func = instr->get_parent()->get_parent();
      if (!bottom_bbs.contains(target[0].bb)) {
        cfg_worklist.push_back(target[0].bb);
        bottom_bbs.insert(target[0].bb);
      }
      // if () {
      // }
      {
        const size_t bb_id = func->bb_id(target[0].bb);
        eval_meets(target[0].bb, bb_id);
      }
      return ConstantValue::Top();
    }
    case fir::InstrType::CondBranchInstr: {
      const auto &targets = instr->get_bb_args();
      ASSERT(targets.size() == 2);
      auto arg = eval(instr->get_arg(0));
      const auto func = instr->get_parent()->get_parent();
      // ASSERT(!arg.is_bottom());
      if (arg.is_bottom()) {
        // utils::Debug << "fixme: SCCP quick fix\n";
        if (!bottom_bbs.contains(targets[0].bb)) {
          cfg_worklist.push_back(targets[0].bb);
          bottom_bbs.insert(targets[0].bb);
        }
        if (!bottom_bbs.contains(targets[1].bb)) {
          cfg_worklist.push_back(targets[1].bb);
          bottom_bbs.insert(targets[1].bb);
        }

        {
          const size_t bb_id = func->bb_id(targets[0].bb);
          eval_meets(targets[0].bb, bb_id);
        }
        {

          const size_t bb_id = func->bb_id(targets[1].bb);
          eval_meets(targets[1].bb, bb_id);
        }
        return ConstantValue::Top();
      }
      if (arg.is_top()) {
        return ConstantValue::Top();
      }

      ASSERT(arg.value->is_int());
      bool cond = arg.value->as_int() != 0;

      if (cond) {
        cfg_worklist.push_back(targets[0].bb);
      }
      if (!cond) {
        cfg_worklist.push_back(targets[1].bb);
      }

      {
        const size_t bb_id = func->bb_id(targets[0].bb);
        eval_meets(targets[0].bb, bb_id);
      }
      {
        const size_t bb_id = func->bb_id(targets[1].bb);
        eval_meets(targets[1].bb, bb_id);
      }

      u8 target_bb_id = cond ? 0 : 1;
      fir::Builder bb{instr};

      auto replacement_term = bb.build_branch(instr->bbs[target_bb_id].bb);
      for (auto bb_arg : instr->bbs[target_bb_id].args) {
        replacement_term.add_bb_arg(0, bb_arg);
      }

      instr.clear_bbs();
      instr.clear_args();
      instr.remove_from_parent();
      // FIXME: properly delete
      instr._invalidate();
      // TODO("handle cond branch being constant or skip top\n");
      values.insert({fir::ValueR(replacement_term), ConstantValue::Top()});
      cfg.update(*replacement_term->get_parent()->get_parent().func, false);
      return ConstantValue::Top();
    }
    case fir::InstrType::Conversion: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (a.is_top()) {
        return ConstantValue::Top();
      }
      switch ((fir::ConversionSubType)instr->get_instr_subtype()) {
      case fir::ConversionSubType::INVALID:
        UNREACH();
      case fir::ConversionSubType::FPTOUI:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>(a.value->as_float()), instr->get_type()));
      case fir::ConversionSubType::FPTOSI:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i64>(a.value->as_float()), instr->get_type()));
      case fir::ConversionSubType::UITOFP:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<f64>(a.value->as_int()), instr->get_type()));
      case fir::ConversionSubType::SITOFP:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<f64>(a.value->as_int()), instr->get_type()));
        break;
      }
      // TODO: impl
      return ConstantValue::Bottom();
    }
    case fir::InstrType::ZExt:
    case fir::InstrType::SExt: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (a.is_top()) {
        return ConstantValue::Top();
      }
      a.value->type = instr->get_type();
      return a;
    }
    case fir::InstrType::FCmp: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() || !b.is_const()) {
        return ConstantValue::Top();
      }

      switch ((fir::FCmpInstrSubType)instr->get_instr_subtype()) {
        // TODO; how to handle
      case fir::FCmpInstrSubType::OGT:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(a.value->as_float() > b.value->as_float()),
            ctx->get_int_type(8)));
      case fir::FCmpInstrSubType::OLT:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(a.value->as_float() < b.value->as_float()),
            ctx->get_int_type(8)));
      case fir::FCmpInstrSubType::OEQ:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(a.value->as_float() == b.value->as_float()),
            ctx->get_int_type(8)));
      case fir::FCmpInstrSubType::OGE:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(a.value->as_float() >= b.value->as_float()),
            ctx->get_int_type(8)));
      case fir::FCmpInstrSubType::OLE:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(a.value->as_float() <= b.value->as_float()),
            ctx->get_int_type(8)));
      case fir::FCmpInstrSubType::ONE:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(a.value->as_float() != b.value->as_float()),
            ctx->get_int_type(8)));
      case fir::FCmpInstrSubType::UEQ:
      case fir::FCmpInstrSubType::UGT:
      case fir::FCmpInstrSubType::UGE:
      case fir::FCmpInstrSubType::ULT:
      case fir::FCmpInstrSubType::ULE:
      case fir::FCmpInstrSubType::UNE:
      case fir::FCmpInstrSubType::ORD:
      case fir::FCmpInstrSubType::UNO:
        return ConstantValue::Bottom();
      case fir::FCmpInstrSubType::AlwFalse:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(false), ctx->get_int_type(8)));
      case fir::FCmpInstrSubType::AlwTrue:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(true), ctx->get_int_type(8)));
      case fir::FCmpInstrSubType::INVALID:
        break;
      }
      failure({"Imply fcmp", instr});
      return ConstantValue::Bottom();
    }
    case fir::InstrType::ICmp: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_const() && (fir::ICmpInstrSubType)instr->get_instr_subtype() ==
                              fir::ICmpInstrSubType::ULE) {
        if (a.value->as_int() == 0) {
          return ConstantValue::Constant(ctx->get_constant_value(
              static_cast<u64>(1), ctx->get_int_type(8)));
        }
      } else if (a.is_const() &&
                 (fir::ICmpInstrSubType)instr->get_instr_subtype() ==
                     fir::ICmpInstrSubType::UGT) {
        if (a.value->as_int() == 0) {
          return ConstantValue::Constant(ctx->get_constant_value(
              static_cast<u64>(0), ctx->get_int_type(8)));
        }
      }

      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() || !b.is_const()) {
        return ConstantValue::Top();
      }

      if (!a.value->is_int() || !b.value->is_int()) {
        failure({"Impl icmp on non ints", instr});
        return ConstantValue::Bottom();
      }

      switch ((fir::ICmpInstrSubType)instr->get_instr_subtype()) {
      case fir::ICmpInstrSubType::INVALID:
        UNREACH();
        break;
      case fir::ICmpInstrSubType::NE:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>(a.value->as_int() != b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::EQ:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>(a.value->as_int() == b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::SLT:
        // utils::Debug << a.value->as_int() << " < " << b.value->as_int() <<
        // "\n"; utils::Debug << (i64)a.value->as_int() << " < "
        //              << (i64)b.value->as_int() << "\n";
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>((i64)a.value->as_int() < (i64)b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::ULT:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>(a.value->as_int() < b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::SGT:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>((i64)a.value->as_int() > (i64)b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::UGT:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>(a.value->as_int() > b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::UGE:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>(a.value->as_int() >= b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::ULE:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>(a.value->as_int() <= b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::SGE:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>((i64)a.value->as_int() >= (i64)b.value->as_int()),
            ctx->get_int_type(8)));
      case fir::ICmpInstrSubType::SLE:
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u64>((i64)a.value->as_int() <= (i64)b.value->as_int()),
            ctx->get_int_type(8)));
      }
      break;
    }
    case fir::InstrType::ITrunc: {
      auto a = eval(instr->get_arg(0));

      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (a.is_top()) {
        return ConstantValue::Top();
      }

      if (!a.value->is_int()) {
        failure({"Impl icmp on non ints", instr});
        return ConstantValue::Bottom();
      }

      auto res_type_width = instr->get_type()->as_int();
      u64 mask = ((u64)1 << res_type_width) - 1;
      return ConstantValue::Constant(ctx->get_constant_value(
          a.value->as_int() & mask, ctx->get_int_type(res_type_width)));
    }
    case fir::InstrType::SelectInstr: {
      auto c = eval(instr->get_arg(0));
      auto a = eval(instr->get_arg(1));
      auto b = eval(instr->get_arg(2));

      if (c.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (c.is_top()) {
        return ConstantValue::Top();
      }

      if (c.value->as_int() != 0) {
        return a;
      }
      return b;
    }
    case fir::InstrType::CallInstr:
    case fir::InstrType::AllocaInstr:
    case fir::InstrType::ReturnInstr:
    case fir::InstrType::LoadInstr:
    case fir::InstrType::StoreInstr:
      return ConstantValue::Top();
    }
    assert(false);
  }

  void eval_and_update(fir::Context &ctx, fir::ValueR value) {
    (void)values;
    (void)cfg;

    ConstantValue new_value = ConstantValue::Top();

    if (value.is_constant()) {
      IMPL("constant\n");
    } else if (value.is_instr()) {
      // utils::Debug << "SCCP: "<< value << "\n";
      new_value = eval_instr(ctx, value.as_instr());
      if (new_value.is_const() && value.get_n_uses() > 0) {
        value.replace_all_uses(fir::ValueR{new_value.value});
      }
    } else if (value.is_bb_arg()) {
      UNREACH();
    }

    if (value.is_valid(true) && values.at(value) != new_value) {
      values.at(value) = new_value;
      for (auto &use : *value.get_uses()) {
        if (reachable_bb.contains(use.user->get_parent())) {
          ssa_worklist.push_back(use);
        }
      }
    }
  }

  void dump() {
    utils::Debug << "DUMP SCCP: ";
    for (auto &[val, consta] : values) {
      utils::Debug << val << ": ";
      switch (consta.type) {
      case ConstantValue::ValueType::Top:
        utils::Debug << "TOP\n";
        break;
      case ConstantValue::ValueType::Constant:
        utils::Debug << consta.value << "\n";
        break;
      case ConstantValue::ValueType::Bottom:
        utils::Debug << "BOT\n";
        break;
      }
    }
  }

  void eval_meets(fir::BasicBlock bb, size_t bb_id) {
    TVec<ConstantValue> res;
    res.resize(bb->n_args(), ConstantValue::Top());

    for (u32 pred_id : cfg.bbrs[bb_id].pred) {
      auto &pred = cfg.bbrs[pred_id].bb;
      fir::Instr pred_term = pred->get_terminator();

      auto pred_args = std::find_if(pred_term->get_bb_args().begin(),
                                    pred_term->get_bb_args().end(),
                                    [bb](auto &&v) { return v.bb == bb; });

      ASSERT(pred_args != pred_term->get_bb_args().end());
      ASSERT(pred_args->args.size() == bb->get_args().size());
      for (size_t arg_id = 0; arg_id < bb->get_args().size(); arg_id++) {
        auto &a = res[arg_id];
        auto b = eval(pred_args->args.at(arg_id));

        if (a.is_bottom() || b.is_bottom()) {
          res[arg_id] = ConstantValue::Bottom();
        } else if (a.is_top() || b.is_top()) {
          res[arg_id] = ConstantValue::Top();
        } else if (a.is_const() && b.is_const()) {
          if (a.value->eql(*b.value.operator->())) {
            res[arg_id] = a;
          } else {
            res[arg_id] = ConstantValue::Bottom();
          }
        } else {
          TODO("");
        }
      }
    }

    for (size_t arg_id = 0; arg_id < bb->get_args().size(); arg_id++) {
      auto key = fir::ValueR(bb->args[arg_id]);
      auto new_value = res.at(arg_id);

      if (new_value != values.at(key)) {
        values.at(key) = new_value;
        for (auto &use : *key.get_uses()) {
          if (reachable_bb.contains(use.user->get_parent())) {
            ssa_worklist.push_back(use);
          }
        }
      }
    }
  }

  void execute() {
    for (auto &[val, consta] : values) {
      if (consta.is_const()) {
        fir::ValueR val_non_const = val;
        // utils::Debug << "SCCP replace: " << val_non_const.as_instr();
        // for (auto &use : *val_non_const.get_uses()) {
        //   utils::Debug << use << "\n";
        // }
        val_non_const.replace_all_uses(fir::ValueR(consta.value));
      }
    }
  }

  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("SCCP");
    cfg.update(func, false);
    cfg_worklist.push_back(func.get_entry());
    for (auto bb : func.get_bbs()) {

      for (size_t arg_id = 0; arg_id < bb->get_args().size(); arg_id++) {
        values.insert({fir::ValueR(bb->args[arg_id]), ConstantValue::Top()});
      }
      for (auto instr : bb->instructions) {
        values.insert({fir::ValueR(instr), ConstantValue::Top()});
      }
    }

    while (!cfg_worklist.empty() || !ssa_worklist.empty()) {
      while (!cfg_worklist.empty()) {
        fir::BasicBlock bb = cfg_worklist.front();
        cfg_worklist.pop_front();
        reachable_bb.insert(bb);

        const size_t bb_id = func.bb_id(bb);

        eval_meets(bb, bb_id);

        for (size_t instr_id = 0; instr_id < bb->instructions.size();
             instr_id++) {
          eval_and_update(ctx, fir::ValueR(bb->instructions[instr_id]));
        }

        // TODO("handle terminator phi stuff merging somehow??\n");
      }
      while (!ssa_worklist.empty()) {
        fir::Use &use = ssa_worklist.front();
        ssa_worklist.pop_front();
        if (!use.user.is_valid()) {
          continue;
        }
        eval_and_update(ctx, fir::ValueR(use.user));
      }
    }
    // dump();
    execute();
  }
};
} // namespace foptim::optim
