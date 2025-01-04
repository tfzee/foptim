#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"

namespace foptim::optim {

static void swap_args(fir::Instr instr, u32 a1, u32 a2) {
  using namespace foptim::fir;
  auto v1 = instr->args[a1];
  auto v2 = instr->args[a2];
  instr.replace_arg(a1, v2);
  instr.replace_arg(a2, v1);
}

template <class T>
bool try_constant_eval_binary(fir::Instr instr,
                              fir::BinaryInstrSubType sub_type, T a, T b,
                              fir::TypeR type, fir::Context &ctx) {
  switch (sub_type) {
  case fir::BinaryInstrSubType::IntAdd:
    instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(a + b, type)));
    return true;
  case fir::BinaryInstrSubType::IntMul:
    instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(a * b, type)));
    return true;
  case fir::BinaryInstrSubType::IntSub:
    instr->replace_all_uses(fir::ValueR(ctx->get_constant_value(a - b, type)));
    return true;
  case fir::BinaryInstrSubType::INVALID:
  case fir::BinaryInstrSubType::IntSRem:
  case fir::BinaryInstrSubType::IntSDiv:
  case fir::BinaryInstrSubType::IntUDiv:
  case fir::BinaryInstrSubType::Shl:
  case fir::BinaryInstrSubType::Shr:
  case fir::BinaryInstrSubType::AShr:
  case fir::BinaryInstrSubType::And:
  case fir::BinaryInstrSubType::Or:
  case fir::BinaryInstrSubType::Xor:
  case fir::BinaryInstrSubType::FloatAdd:
  case fir::BinaryInstrSubType::FloatSub:
  case fir::BinaryInstrSubType::FloatMul:
  case fir::BinaryInstrSubType::FloatDiv:
    return false;
  }
}

inline void swap_args_icmp(fir::Instr instr) {
  ASSERT(instr->is(fir::InstrType::ICmp))
  switch ((fir::ICmpInstrSubType)instr->subtype) {
  case fir::ICmpInstrSubType::NE:
  case fir::ICmpInstrSubType::EQ:
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::SLT:
    instr->subtype = (u32)fir::ICmpInstrSubType::SGE;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::ULT:
    instr->subtype = (u32)fir::ICmpInstrSubType::UGE;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::SGT:
    instr->subtype = (u32)fir::ICmpInstrSubType::SLE;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::UGT:
    instr->subtype = (u32)fir::ICmpInstrSubType::ULE;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::UGE:
    instr->subtype = (u32)fir::ICmpInstrSubType::ULT;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::ULE:
    instr->subtype = (u32)fir::ICmpInstrSubType::UGT;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::SGE:
    instr->subtype = (u32)fir::ICmpInstrSubType::SLT;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::SLE:
    instr->subtype = (u32)fir::ICmpInstrSubType::SGT;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::INVALID:
    UNREACH();
  }
}

//@returns true if it removes the isntrction
inline bool simplify_binary(fir::Instr instr, size_t instr_id,
                            fir::BasicBlock bb, fir::Context &ctx) {
  using namespace foptim::fir;
  // since both being constant would be handleded by constant folding we just
  // asume theres one and normalzie by putting it into the secodn arg
  {
    if (instr->is_commutative() && instr->args[0].is_constant() &&
        (!instr->args[0].as_constant()->is_global() ||
         !instr->args[0].as_constant()->is_func())) {
      swap_args(instr, 0, 1);
    }
  }

  const bool has_const = instr->args[1].is_constant();
  // const bool both_const = has_const && instr->args[0].is_constant();
  if (!has_const) {
    return false;
  }
  ConstantValueR c_val = instr->args[1].as_constant();

  if (instr->args[0].is_constant() && instr->args[1].is_constant()) {
    ConstantValueR c0_val = instr->args[0].as_constant();
    if (c_val->type->is_int() && c0_val->type->is_int()) {
      if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                   c0_val->as_int(), c_val->as_int(),
                                   c_val->type, ctx)) {
        bb->remove_instr(instr_id);
        return true;
      }
    } else if (c_val->type->is_float() && c0_val->type->is_float()) {
      if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                   c0_val->as_float(), c_val->as_float(),
                                   c_val->type, ctx)) {
        bb->remove_instr(instr_id);
        return true;
      }
    }
  }
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
inline bool simplify_icmp(fir::Instr instr, size_t /* instr_id*/,
                          fir::BasicBlock bb, fir::Context &ctx) {
  (void)bb;
  using namespace foptim::fir;
  {
    if (instr->args[0].is_constant() &&
        (!instr->args[0].as_constant()->is_global() ||
         !instr->args[0].as_constant()->is_func())) {
      swap_args_icmp(instr);
    }
  }

  bool first_constant = instr->args[0].is_constant();
  bool second_constant = instr->args[1].is_constant();
  // utils::Debug << instr << " " << first_constant << " " << second_constant
  //              << "\n";

  if (first_constant && second_constant) {
    const auto c1 = instr->args[0].as_constant();
    const auto c2 = instr->args[1].as_constant();
    ASSERT(c1->is_int());
    const auto v1 = c1->as_int();
    const auto v2 = c2->as_int();

    bool is_true = false;
    switch ((ICmpInstrSubType)instr->get_instr_subtype()) {
    case fir::ICmpInstrSubType::INVALID:
      UNREACH();
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
    auto new_const_value =
        ctx->get_constant_value((u64)is_true, ctx->get_int_type(8));
    instr->replace_all_uses(ValueR(new_const_value));
    ASSERT(instr->bbs.size() == 0);
    instr.remove_from_parent();
    return true;
  }
  return false;
}

// FIXME: maybe fix this but simplify cfg also handles it
//        but idk the issue might be bb args
//@returns true if it removes the isntrction
//  inline bool simplify_cond_branch(fir::Instr instr, size_t /*instr_id*/,
//                                   fir::BasicBlock bb, fir::Context &ctx) {
//    (void)ctx;
//    // replace conditional branch to simple branch
//    if (instr->args[0].is_constant()) {
//      fir::Builder b(bb);
//      b.at_end(bb);

//     auto v1 = instr->args[0].as_constant()->as_int();
//     auto &target = instr->bbs[0];
//     if (v1 == 0) {
//       target = instr->bbs[1];
//     }

//     auto new_branch = b.build_branch(target.bb);
//     for (auto old_arg : target.args) {
//       new_branch.add_bb_arg(0, old_arg);
//     }
//     instr.remove_from_parent();
//     // bb->remove_instr(instr_id);
//     return true;
//   }
//   return false;
// }

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
  // if (instr->get_instr_type() == InstrType::CondBranchInstr) {
  //   return simplify_cond_branch(instr, instr_id, bb, ctx);
  // }
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
