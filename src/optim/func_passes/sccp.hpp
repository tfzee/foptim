#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <unordered_set>

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

    constexpr bool is_top() const { return type == ValueType::Top; }
    constexpr bool is_bottom() const { return type == ValueType::Bottom; }
    constexpr bool is_const() const { return type == ValueType::Constant; }

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
  // TODO: prob can replace edge with just user
  std::deque<fir::Use> ssa_worklist{};
  std::deque<fir::BasicBlock> cfg_worklist;
  FMap<fir::ValueR, ConstantValue> values;

  std::unordered_set<fir::BasicBlock> reachable_bb{};

public:
  ConstantValue eval(fir::ValueR value) {
    if (value.is_constant()) {
      return ConstantValue::Constant(value.as_constant());
    } else if (values.contains(value)) {
      return values.at(value);
    }

    return ConstantValue::Bottom();
  }

  void eval_and_update(fir::Context &ctx, fir::ValueR value) {
    (void)values;
    (void)cfg;

    ConstantValue new_value = ConstantValue::Top();

    if (value.is_constant()) {
      TODO("constant\n");
    } else if (value.is_instr()) {
      auto instr = value.as_instr();
      switch (instr->get_instr_type()) {
      case fir::InstrType::BinaryInstr: {
        auto a = eval(instr->get_arg(0));
        auto b = eval(instr->get_arg(1));

        if (a.is_bottom() || b.is_bottom()) {
          new_value = ConstantValue::Bottom();
        } else if (a.is_const() && b.is_const()) {
          switch ((fir::BinaryInstrSubType)instr->get_instr_subtype()) {
          case fir::BinaryInstrSubType::INVALID:
            TODO("¿UNREACH?\n");
            break;
          case fir::BinaryInstrSubType::IntAdd:
            if (!a.value->is_int() || !b.value->is_int()) {
              break;
            }
            new_value = ConstantValue::Constant(ctx->get_constant_value(
                a.value->as_int() + b.value->as_int(), a.value->get_type()));
            break;
          case fir::BinaryInstrSubType::IntMul:
            if (!a.value->is_int() || !b.value->is_int()) {
              break;
            }
            new_value = ConstantValue::Constant(ctx->get_constant_value(
                a.value->as_int() * b.value->as_int(), a.value->get_type()));
            break;
          }
        } else {
          new_value = ConstantValue::Top();
        }
        break;
      }
      case fir::InstrType::BranchInstr: {
        auto &target = instr->get_bb_args();
        // utils::Debug << " HIT BRANCH\n\n";
        ASSERT(target.size() == 1);
        if (!reachable_bb.contains(target[0].bb)) {
          cfg_worklist.push_back(target[0].bb);
        }
        {
          const size_t bb_id = std::find_if(cfg.bbrs.begin(), cfg.bbrs.end(),
                                            [&target](auto &&value) {
                                              return value.bb == target[0].bb;
                                            }) -
                               cfg.bbrs.begin();
          eval_meets(target[0].bb, bb_id);
        }
        break;
      }
      case fir::InstrType::CondBranchInstr: {
        auto &targets = instr->get_bb_args();
        ASSERT(targets.size() == 2);
        auto arg = eval(instr->get_arg(0));
        // ASSERT(!arg.is_bottom());
        if (arg.is_bottom()) {
          if (!reachable_bb.contains(targets[0].bb)) {
            cfg_worklist.push_back(targets[0].bb);
          }
          if (!reachable_bb.contains(targets[1].bb)) {
            cfg_worklist.push_back(targets[1].bb);
          }

          {
            const size_t bb_id =
                std::find_if(cfg.bbrs.begin(), cfg.bbrs.end(),
                             [&targets](auto &&value) {
                               return value.bb == targets[0].bb;
                             }) -
                cfg.bbrs.begin();
            eval_meets(targets[0].bb, bb_id);
          }
          {
            const size_t bb_id =
                std::find_if(cfg.bbrs.begin(), cfg.bbrs.end(),
                             [&targets](auto &&value) {
                               return value.bb == targets[1].bb;
                             }) -
                cfg.bbrs.begin();
            eval_meets(targets[1].bb, bb_id);
          }
        } else {
          TODO("handle cond branch being constant or skip top\n");
        }

        break;
      }
      case fir::InstrType::SExt: {
        new_value = eval(instr->get_arg(0));
        if (new_value.is_const()) {
          new_value.value->type = ctx->copy(instr->get_type());
        }
        break;
      }

      case fir::InstrType::DirectCallInstr:
      case fir::InstrType::AllocaInstr:
      case fir::InstrType::ReturnInstr:
      case fir::InstrType::LoadInstr:
      case fir::InstrType::StoreInstr:
      case fir::InstrType::ICmp:
        new_value = ConstantValue::Bottom();
        break;
      }
    } else if (value.is_bb_arg()) {
      TODO("UNREACH\n");
    }

    if (new_value != values.at(value)) {
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
    FVec<ConstantValue> res;
    res.resize(bb->n_args(), ConstantValue::Top());

    for (u32 pred_id : cfg.bbrs[bb_id].pred) {
      auto &pred = cfg.bbrs[pred_id].bb;
      fir::Instr pred_term = pred->get_terminator();

      auto pred_args = std::find_if(pred_term->get_bb_args().begin(),
                                    pred_term->get_bb_args().end(),
                                    [bb](auto &&v) { return v.bb == bb; });

      ASSERT(pred_args != pred_term->get_bb_args().end());
      for (size_t arg_id = 0; arg_id < bb->get_args().size(); arg_id++) {
        auto a = res[arg_id];

        auto b = eval(pred_args->args[arg_id]);

        if (a.is_bottom() || b.is_bottom()) {
          res[arg_id] = ConstantValue::Bottom();
        } else if (a.is_top()) {
          res[arg_id] = b;
        } else if (b.is_top()) {
          res[arg_id] = a;
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
      auto key = fir::ValueR(bb, arg_id);
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
        utils::Debug << "SCCP replace: " << val_non_const.as_instr();
        for (auto &use : *val_non_const.get_uses()) {
          utils::Debug << use << "\n";
        }
        val_non_const.replace_all_uses(fir::ValueR(consta.value));
      }
    }
  }

  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("SCCP");
    cfg.update(func, false);
    cfg_worklist.push_back(func.get_entry_bb());
    for (auto bb : func.get_bbs()) {

      for (size_t arg_id = 0; arg_id < bb->get_args().size(); arg_id++) {
        values.insert({fir::ValueR(bb, arg_id), ConstantValue::Top()});
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

        const size_t bb_id =
            std::find(func.basic_blocks.begin(), func.basic_blocks.end(), bb) -
            func.basic_blocks.begin();
        eval_meets(bb, bb_id);

        for (auto &instr : bb->instructions) {
          eval_and_update(ctx, fir::ValueR(instr));
        }

        // TODO("handle terminator phi stuff merging somehow??\n");
      }
      while (!ssa_worklist.empty()) {
        fir::Use &use = ssa_worklist.front();
        ssa_worklist.pop_front();
        eval_and_update(ctx, fir::ValueR(use.user));
      }
    }
    // dump();
    execute();
  }
};
} // namespace foptim::optim
