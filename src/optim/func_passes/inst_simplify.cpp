#include "inst_simplify.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"

namespace foptim::optim {

using WorkList = TVec<InstSimplify::WorkItem>;

static void swap_args(fir::Instr instr, u32 a1, u32 a2) {
  using namespace foptim::fir;
  auto v1 = instr->args[a1];
  auto v2 = instr->args[a2];
  instr.replace_arg(a1, v2);
  instr.replace_arg(a2, v1);
}

static void swap_args_icmp(fir::Instr instr) {
  ASSERT(instr->is(fir::InstrType::ICmp))
  switch ((fir::ICmpInstrSubType)instr->subtype) {
  case fir::ICmpInstrSubType::NE:
  case fir::ICmpInstrSubType::EQ:
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::SLT:
    instr->subtype = (u32)fir::ICmpInstrSubType::SGT;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::ULT:
    instr->subtype = (u32)fir::ICmpInstrSubType::UGT;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::SGT:
    instr->subtype = (u32)fir::ICmpInstrSubType::SLT;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::UGT:
    instr->subtype = (u32)fir::ICmpInstrSubType::ULT;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::UGE:
    instr->subtype = (u32)fir::ICmpInstrSubType::ULE;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::ULE:
    instr->subtype = (u32)fir::ICmpInstrSubType::UGE;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::SGE:
    instr->subtype = (u32)fir::ICmpInstrSubType::SLE;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::SLE:
    instr->subtype = (u32)fir::ICmpInstrSubType::SGE;
    swap_args(instr, 0, 1);
    return;
  case fir::ICmpInstrSubType::INVALID:
    UNREACH();
  }
}
static void push_all_uses(WorkList &worklist, fir::Instr instr) {
  for (auto &use : instr->uses) {
    worklist.emplace_back(use.user, use.user->parent);
  }
}

static void simplify_binary(fir::Instr instr, fir::BasicBlock /*bb*/,
                            fir::Context &ctx, WorkList &worklist) {
  using namespace foptim::fir;
  // since both being constant would be handleded by constant folding we just
  // asume theres one and normalzie by putting it into the secodn arg
  {
    if (instr->is_commutative() && instr->args[0].is_constant() &&
        !instr->args[0].as_constant()->is_global() &&
        !instr->args[0].as_constant()->is_func()) {
      swap_args(instr, 0, 1);
    }
  }

  const ConstantValue *c0_val = nullptr;
  const ConstantValue *c1_val = nullptr;
  if (instr->args[0].is_constant()) {
    c0_val = instr->args[0].as_constant().get_raw_ptr();
  }
  if (instr->args[1].is_constant()) {
    c1_val = instr->args[1].as_constant().get_raw_ptr();
  }
  const bool has_const = (c0_val != nullptr) || (c1_val != nullptr);
  // const bool both_const = has_const && instr->args[0].is_constant();
  if (!has_const) {
    return;
  }

  if ((c0_val != nullptr) && (c1_val != nullptr)) {
    if (c1_val->type->is_int() && c0_val->type->is_int()) {
      if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                   c0_val->as_int(), c1_val->as_int(),
                                   c1_val->type, ctx)) {
        instr.remove_from_parent();
        return;
      }
    } else if (c1_val->type->is_float() && c0_val->type->is_float()) {
      if (try_constant_eval_binary(instr, (BinaryInstrSubType)instr->subtype,
                                   c0_val->as_float(), c1_val->as_float(),
                                   c1_val->type, ctx)) {
        instr.remove_from_parent();
        return;
      }
    }
  }

  const auto *c_val = (c1_val != nullptr) ? c1_val : c0_val;
  // const u32 c_idx = (c0_val != nullptr) ? 0 : 1;
  const u32 v_idx = (c1_val != nullptr) ? 0 : 1;

  // at this point it cant have both as constant
  if (c_val->is_float()) {
    if (c_val->as_float() == 0 &&
        instr->get_instr_subtype() == (u32)BinaryInstrSubType::FloatAdd) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[v_idx]);
      instr.remove_from_parent();
      return;
    }
    if (instr->get_instr_subtype() == (u32)BinaryInstrSubType::FloatMul) {
      if (c_val->as_float() == 1) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[v_idx]);
        instr.remove_from_parent();
      } else if (c_val->as_float() == 0) {
        auto zero_const = ctx.data->get_constant_value(.0, c_val->get_type());
        push_all_uses(worklist, instr);
        instr->replace_all_uses(ValueR{zero_const});
        instr.remove_from_parent();
      }
      return;
    }
  }

  if (c_val->is_int() &&
      instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntAdd) {
    if (instr->args[0].is_instr() && c1_val->get_type()->is_int()) {
      ASSERT(c0_val == nullptr);
      auto a0 = instr->args[0].as_instr();
      if (a0->is(InstrType::BinaryInstr) && a0->args[1].is_constant() &&
          a0->args[1].as_constant()->is_int()) {
        auto sec_constant = a0->args[1].as_constant()->as_int();
        // fmt::println("{}", instr);
        // FIXME: fix potential issue with overflow
        auto biggest_bitwidth = std::max(a0->args[1].get_type()->as_int(),
                                         c1_val->get_type()->as_int());

        switch ((BinaryInstrSubType)a0->subtype) {
        case fir::BinaryInstrSubType::INVALID:
        case fir::BinaryInstrSubType::FloatAdd:
        case fir::BinaryInstrSubType::FloatSub:
        case fir::BinaryInstrSubType::FloatMul:
        case fir::BinaryInstrSubType::FloatDiv:
          UNREACH();
        case fir::BinaryInstrSubType::IntAdd: {
          auto new_val =
              ctx->get_constant_value(c1_val->as_int() + sec_constant,
                                      ctx->get_int_type(biggest_bitwidth));
          instr.replace_arg(0, a0->args[0]);
          instr.replace_arg(1, ValueR(new_val));
          push_all_uses(worklist, instr);
          break;
        }
        case fir::BinaryInstrSubType::IntSub: {
          auto new_val =
              ctx->get_constant_value(c1_val->as_int() - sec_constant,
                                      ctx->get_int_type(biggest_bitwidth));
          instr.replace_arg(0, a0->args[0]);
          instr.replace_arg(1, ValueR(new_val));
          push_all_uses(worklist, instr);
          break;
        }
        default:
          break;
        }
      }
    }
    // const auto *c_val = (c0_val != nullptr) ? c0_val : c1_val;
    if (c1_val->as_int() == 0) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[v_idx]);
      instr.remove_from_parent();
      return;
    }
  }
  if (c_val->is_int() &&
      instr->get_instr_subtype() == (u32)BinaryInstrSubType::IntMul) {
    if (c_val->as_int() == 1) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[v_idx]);
      instr.remove_from_parent();
      return;
    }
    if (c_val->as_int() == 0) {
      auto zero_const = ctx.data->get_constant_value(0, c_val->get_type());
      push_all_uses(worklist, instr);
      instr->replace_all_uses(ValueR{zero_const});
      instr.remove_from_parent();
      return;
    }
  }
}

static void simplify_icmp(fir::Instr instr, fir::BasicBlock /*bb*/,
                          fir::Context &ctx, WorkList &worklist) {
  using namespace foptim::fir;
  {
    if ((instr->args[0].is_constant() &&
         (!instr->args[0].as_constant()->is_global() &&
          !instr->args[0].as_constant()->is_func())) &&
        (!instr->args[1].is_constant() ||
         (instr->args[1].as_constant()->is_global() ||
          instr->args[1].as_constant()->is_func()))) {
      swap_args_icmp(instr);
    }
  }

  bool first_constant = instr->args[0].is_constant();
  bool second_constant = instr->args[1].is_constant();

  if (first_constant && second_constant) {
    const auto c1 = instr->args[0].as_constant();
    const auto c2 = instr->args[1].as_constant();
    // TODO: poisson should just return a poisson value
    if (c1->is_poison() || c2->is_poison()) {
      auto new_const_value = ctx->get_poisson_value(ctx->get_int_type(8));
      push_all_uses(worklist, instr);
      instr->replace_all_uses(ValueR(new_const_value));
      ASSERT(instr->bbs.size() == 0);
      instr.remove_from_parent();
      return;
    }

    if (c1->is_global() || c1->is_func() || c2->is_global() || c2->is_func()) {
      return;
    }
    ASSERT(c1->is_int());
    ASSERT(c2->is_int());
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
      is_true = (u64)v1 < (u64)v2;
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
      is_true = (u64)v1 > (u64)v2;
      break;
    case fir::ICmpInstrSubType::UGE:
      is_true = (u64)v1 >= (u64)v2;
      break;
    case fir::ICmpInstrSubType::ULE:
      is_true = (u64)v1 <= (u64)v2;
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
    push_all_uses(worklist, instr);
    instr->replace_all_uses(ValueR(new_const_value));
    ASSERT(instr->bbs.size() == 0);
    instr.remove_from_parent();
    return;
  }

  // if first is not a constant we could get still a
  //  y = add x 5
  //  icmp y 100 -> which then can be simplified
  // if (!first_constant && second_constant && instr->args[0].is_instr()) {
  //   auto arg_instr = instr->args[0].as_instr();
  //   auto constant = instr->args[1].as_constant()->as_int();

  //   if (arg_instr->is(InstrType::BinaryInstr) &&
  //       arg_instr->args[1].is_constant() &&
  //       arg_instr->args[1].as_constant()->is_int() &&
  //       (BinaryInstrSubType)arg_instr->subtype == BinaryInstrSubType::IntAdd)
  //       {
  //     auto constant2 = arg_instr->args[1].as_constant()->as_int();
  //     instr.replace_arg(0, arg_instr->args[0]);
  //     instr.replace_arg(1, ValueR(ctx->get_constant_value(
  //                              constant - constant2,
  //                              arg_instr->get_type())));
  //     return;
  //   }
  // }
}

static void simplify_fcmp(fir::Instr instr, fir::BasicBlock /*bb*/,
                          fir::Context &ctx, WorkList &worklist) {
  using namespace foptim::fir;
  {
    // TODO: swap arges
  }

  bool first_constant = instr->args[0].is_constant();
  bool second_constant = instr->args[1].is_constant();

  if (first_constant && second_constant) {
    const auto c1 = instr->args[0].as_constant();
    const auto c2 = instr->args[1].as_constant();
    ASSERT(c1->is_float());
    const auto v1 = c1->as_float();
    const auto v2 = c2->as_float();

    bool is_true = false;
    // IMPORTANT: !!THIS IS IN OTHER SYNTAX SO FLIPPED ARGUMETNS!!
    // IMPORTANT: !!THIS IS IN OTHER SYNTAX SO FLIPPED ARGUMETNS!!
    switch ((FCmpInstrSubType)instr->get_instr_subtype()) {
    case fir::FCmpInstrSubType::IsNaN:
      __asm__("vcomisd %2, %1\n\t"
              "setp %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OEQ:
      __asm__("vcomisd %2, %1\n\t"
              "sete %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OGT:
      __asm__("vcomisd %2, %1\n\t"
              "seta %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OGE:
      __asm__("vcomisd %2, %1\n\t"
              "setae %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OLT:
      __asm__("vcomisd %2, %1\n\t"
              "setb %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::OLE:
      __asm__("vcomisd %2, %1\n\t"
              "setbe %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::ONE:
      __asm__("vcomisd %2, %1\n\t"
              "setne %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::ORD:
    case fir::FCmpInstrSubType::UNO:
    case fir::FCmpInstrSubType::UEQ:
    case fir::FCmpInstrSubType::UGT:
    case fir::FCmpInstrSubType::UGE:
    case fir::FCmpInstrSubType::ULT:
    case fir::FCmpInstrSubType::ULE:
      IMPL("implement");
    case fir::FCmpInstrSubType::UNE:
      __asm__("vucomiss %2, %1\n\t"
              "setne %0"
              : "=r"(is_true)
              : "x"(v1), "x"(v2));
      break;
    case fir::FCmpInstrSubType::INVALID:
    case fir::FCmpInstrSubType::AlwFalse:
    case fir::FCmpInstrSubType::AlwTrue:
      return;
    }
    auto new_const_value =
        ctx->get_constant_value((u64)is_true, ctx->get_int_type(8));
    push_all_uses(worklist, instr);
    instr->replace_all_uses(ValueR(new_const_value));
    ASSERT(instr->bbs.size() == 0);
    instr.remove_from_parent();
    return;
  }
}

static void simplify_select(fir::Instr instr, fir::BasicBlock /*bb*/,
                            fir::Context & /*ctx*/, WorkList &worklist) {
  if (instr->args[0].is_constant()) {
    auto v1 = instr->args[0].as_constant()->as_int();
    push_all_uses(worklist, instr);
    if (v1 == 0) {
      instr->replace_all_uses(instr->args[2]);
    } else {
      instr->replace_all_uses(instr->args[1]);
    }
    instr.remove_from_parent();
    return;
  }
  if (instr->args[1] == instr->args[2]) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[1]);
    instr.remove_from_parent();
    return;
  }
}

static void simplify_cond_branch(fir::Instr instr, fir::BasicBlock bb,
                                 fir::Context & /*ctx*/,
                                 WorkList & /*worklist*/) {
  if (instr->args[0].is_constant()) {
    fir::Builder b(bb);
    b.at_end(bb);

    auto v1 = instr->args[0].as_constant()->as_int();
    auto *target = &instr->bbs[0];
    if (v1 == 0) {
      target = &instr->bbs[1];
    }

    auto new_branch = b.build_branch(target->bb);
    for (auto old_arg : target->args) {
      new_branch.add_bb_arg(0, old_arg);
    }
    instr.remove_from_parent();
    return;
  }
}
static void simplify_extend(fir::Instr instr, fir::BasicBlock /*bb*/,
                            fir::Context & /*ctx*/, WorkList &worklist) {
  // TODO: could also maybe figure out cases where we can convert everything
  // into higher bitwidth
  if (instr->args[0].is_constant()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[0]);
    instr.remove_from_parent();
  }
}

static void simplify(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                     WorkList &worklist) {
  using namespace foptim::fir;
  if (instr->get_instr_type() == InstrType::BinaryInstr) {
    return simplify_binary(instr, bb, ctx, worklist);
  }
  if (instr->get_instr_type() == InstrType::ICmp) {
    return simplify_icmp(instr, bb, ctx, worklist);
  }
  if (instr->get_instr_type() == InstrType::FCmp) {
    return simplify_fcmp(instr, bb, ctx, worklist);
  }
  if (instr->get_instr_type() == InstrType::SelectInstr) {
    return simplify_select(instr, bb, ctx, worklist);
  }
  if (instr->get_instr_type() == InstrType::CondBranchInstr) {
    return simplify_cond_branch(instr, bb, ctx, worklist);
  }
  if (instr->get_instr_type() == InstrType::SExt ||
      instr->get_instr_type() == InstrType::ZExt) {
    return simplify_extend(instr, bb, ctx, worklist);
  }
}

void InstSimplify::apply(fir::Context &ctx, fir::Function &func) {
  using namespace foptim::fir;

  // TODO: maybe replace with actual queue
  TVec<WorkItem> worklist;
  for (BasicBlock bb : func.basic_blocks) {
    auto &instrs = bb->get_instrs();
    for (size_t instr_id = 0; instr_id < instrs.size(); instr_id++) {
      auto instr = instrs.at(instr_id);
      worklist.emplace_back(instr, bb);
    }
  }

  while (!worklist.empty()) {
    auto [instr, bb] = worklist.back();
    worklist.pop_back();
    if (!instr->parent.is_valid()) {
      continue;
    }
    simplify(instr, bb, ctx, worklist);
  }
}

} // namespace foptim::optim
