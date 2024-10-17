#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"

namespace foptim::optim {

static void swap_args(fir::Instr instr, u32 a1, u32 a2) {
  using namespace foptim::fir;
  auto v1 = instr->args[a1];
  auto v2 = instr->args[a2];
  instr.replace_arg(a1, v2);
  instr.replace_arg(a2, v1);
}

//@returns true if it removes the isntrction
static bool simplify_binary(fir::Instr instr, size_t instr_id,
                            fir::BasicBlock bb, fir::Context &ctx) {
  using namespace foptim::fir;
  // since both being constant would be handleded by constant folding we just
  // asume theres one and normalzie by putting it into the secodn arg
  {
    if (instr->args[0].is_constant()) {
      // if(instr->args[1].is_constant() && instr->args[1].as_constant())
      swap_args(instr, 0, 1);
    }
  }

  const bool has_const = instr->args[1].is_constant();
  // const bool both_const = has_const && instr->args[0].is_constant();
  if (!has_const) {
    return false;
  }
  ConstantValueR c_val = instr->args[1].as_constant();

  if (c_val->is_int() &&
      instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntAdd) {
    if (c_val->as_int() == 0) {
      instr->replace_all_uses(instr->args[0]);
      bb->remove_instr(instr_id);
      return true;
    }
  }
  if (c_val->is_int() &&
      instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntMul) {
    if (c_val->as_int() == 1) {
      instr->replace_all_uses(instr->args[0]);
      bb->remove_instr(instr_id);
      return true;
    }
    if (c_val->as_int() == 0) {
      auto zero_const = ctx.data->get_constant_value(0, c_val->get_type());
      instr->replace_all_uses(ValueR{zero_const});
      bb->remove_instr(instr_id);
      return true;
    }
  }
  return false;
}

//@returns true if it removes the isntrction
static bool simplify_icmp(fir::Instr instr, size_t instr_id, fir::BasicBlock bb,
                          fir::Context &ctx) {
  (void)bb;
  using namespace foptim::fir;
  if (instr->args[0].is_constant() && instr->args[1].is_constant()) {
    const auto c1 = instr->args[0].as_constant();
    const auto c2 = instr->args[1].as_constant();
    ASSERT(c1->is_int());
    const auto v1 = c1->as_int();
    const auto v2 = c2->as_int();

    bool is_true = false;
    switch ((ICmpInstrSubType)instr->get_instr_subtype()) {
    case fir::ICmpInstrSubType::INVALID:
      ASSERT(false);
    case fir::ICmpInstrSubType::SLT:
      is_true = (i64)v1 < (i64)v2;
      break;
    case fir::ICmpInstrSubType::ULT:
      is_true = v1 < v2;
      break;
    case fir::ICmpInstrSubType::NE:
      is_true = v1 != v2;
      break;
    case fir::ICmpInstrSubType::EQ:
      is_true = v1 == v2;
      break;
    case fir::ICmpInstrSubType::SGT:
      is_true = (i64)v1 > (i64)v2;
      break;
    case fir::ICmpInstrSubType::UGT:
      is_true = v1 > v2;
      break;
    case fir::ICmpInstrSubType::UGE:
      is_true = v1 >= v2;
      break;
    case fir::ICmpInstrSubType::ULE:
      is_true = v1 <= v2;
      break;
    case fir::ICmpInstrSubType::SGE:
      is_true = (i64)v1 >= (i64)v2;
      break;
    case fir::ICmpInstrSubType::SLE:
      is_true = (i64)v1 <= (i64)v2;
      break;
    }
    if (is_true) {
      auto new_const_value =
          ctx->get_constant_value((u64)is_true, ctx->get_int_type(8));
      instr->replace_all_uses(ValueR(new_const_value));
      bb->remove_instr(instr_id);
      return true;
    }
  }
  return false;
}

//@returns true if it removes the isntrction
static bool simplify_cond_branch(fir::Instr instr, size_t instr_id,
                                 fir::BasicBlock bb, fir::Context &ctx) {
  (void)ctx;
  if (instr->args[0].is_constant()) {
    fir::Builder b(bb);
    b.at_end(bb);

    auto v1 = instr->args[0].as_constant()->as_int();
    auto &target = instr->bbs[0];
    if (v1 == 0) {
      target = instr->bbs[1];
    }

    auto new_branch = b.build_branch(target.bb);
    for (auto old_arg : target.args) {
      new_branch.add_bb_arg(0, old_arg);
    }
    bb->remove_instr(instr_id);
    return true;
  }
  return false;
}

//@returns true if it removes the isntrction
static bool simplify(fir::Instr instr, size_t instr_id, fir::BasicBlock bb,
                     fir::Context &ctx) {
  using namespace foptim::fir;
  if (instr->get_instr_type() == InstrType::BinaryInstr) {
    return simplify_binary(instr, instr_id, bb, ctx);
  }
  if (instr->get_instr_type() == InstrType::ICmp) {
    return simplify_icmp(instr, instr_id, bb, ctx);
  }
  if (instr->get_instr_type() == InstrType::CondBranchInstr) {
    return simplify_cond_branch(instr, instr_id, bb, ctx);
  }
  return false;
}

class InstSimplify final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    using namespace foptim::fir;
    // COULD DO IT RECURSIVELY WORKLIST -> APPEND USEs

    for (BasicBlock bb : func.basic_blocks) {
      auto &instrs = bb->get_instrs();
      for (size_t instr_id = 0; instr_id < instrs.size(); instr_id++) {
        auto instr = instrs.at(instr_id);
        if (simplify(instr, instr_id, bb, ctx)) {
          instr_id--;
        }
      }
    }
  }
};

} // namespace foptim::optim
