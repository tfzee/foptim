#include "inst_simplify.hpp"

#include <fmt/base.h>
#include <fmt/core.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <bit>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/attributer/KnownBits.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/func_passes/inst_simplify/simplify_binary.hpp"
#include "optim/func_passes/inst_simplify/simplify_fcmp.hpp"
#include "optim/func_passes/inst_simplify/simplify_icmp.hpp"
#include "utils/set.hpp"
#include "utils/tracy.hpp"

namespace foptim::optim {
namespace InstSimp {
namespace {

// if we got convert(load(v as x) to y) so loading v as type x and then
// converting to y. If the conversion preserves the bitwise representation
// (forexample bitcast ptrtoint intopt...r)
//  then we can use this to just create a load of the type y
bool load_into_conversion_simpl(fir::Instr instr, fir::Instr a0,
                                WorkList &worklist) {
  if (a0->get_n_uses() == 1 || (!a0->Volatile && !a0->Atomic)) {
    fir::Builder buh{instr};
    auto new_val = buh.build_load(instr->get_type(), a0->args[0], a0->Atomic,
                                  a0->Volatile);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(new_val);
    instr.destroy();
    if (a0->get_n_uses() == 0) {
      a0.destroy();
    }
    return true;
  }
  return false;
}

bool is_constant_int_zero(fir::ValueR r) {
  if (!r.is_constant()) {
    return false;
  }
  auto c = r.as_constant();
  if (!c->is_int()) {
    return false;
  }
  return c->as_int() == 0;
}

bool is_constant_float_zero(fir::ValueR r) {
  if (!r.is_constant()) {
    return false;
  }
  auto c = r.as_constant();
  if (!c->is_float()) {
    return false;
  }
  return c->as_float() == 0;
}

bool select_to_abs(fir::Instr instr, WorkList &worklist) {
  auto icmp = instr->args[0].as_instr();
  bool negated = false;
  bool positive = false;
  // fmt::println("{:cd}", icmp);
  // fmt::println("{:cd}", instr);
  if (icmp->args[0] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::UnaryInstrSubType::IntNeg) &&
      instr->args[2].as_instr()->args[0] == icmp->args[0]) {
    positive = true;
  }
  if (icmp->args[0] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::BinaryInstrSubType::IntSub) &&
      is_constant_int_zero(instr->args[2].as_instr()->args[0]) &&
      instr->args[2].as_instr()->args[1] == icmp->args[0]) {
    positive = true;
  }
  if (icmp->args[0] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::UnaryInstrSubType::IntNeg) &&
      instr->args[1].as_instr()->args[0] == icmp->args[0]) {
    negated = true;
  }
  if (icmp->args[0] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::BinaryInstrSubType::IntSub) &&
      is_constant_int_zero(instr->args[1].as_instr()->args[0]) &&
      instr->args[1].as_instr()->args[1] == icmp->args[0]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::UnaryInstrSubType::IntNeg) &&
      instr->args[2].as_instr()->args[0] == icmp->args[1]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::BinaryInstrSubType::IntSub) &&
      is_constant_int_zero(instr->args[2].as_instr()->args[0]) &&
      instr->args[2].as_instr()->args[1] == icmp->args[1]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::UnaryInstrSubType::IntNeg) &&
      instr->args[1].as_instr()->args[0] == icmp->args[1]) {
    positive = true;
  }
  if (icmp->args[1] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::BinaryInstrSubType::IntSub) &&
      is_constant_int_zero(instr->args[1].as_instr()->args[0]) &&
      instr->args[1].as_instr()->args[1] == icmp->args[1]) {
    positive = true;
  }
  ASSERT(!negated || !positive);
  if (negated || positive) {
    fir::Builder b{instr};
    fir::ValueR new_val;
    switch ((fir::ICmpInstrSubType)icmp->subtype) {
      case fir::ICmpInstrSubType::SGT:
      case fir::ICmpInstrSubType::SGE:
      case fir::ICmpInstrSubType::UGT:
      case fir::ICmpInstrSubType::UGE:
        if (positive) {
          new_val =
              b.build_intrinsic(icmp->args[0], fir::IntrinsicSubType::Abs);
          break;
        }
        break;
      case fir::ICmpInstrSubType::SLT:
      case fir::ICmpInstrSubType::SLE:
      case fir::ICmpInstrSubType::ULT:
      case fir::ICmpInstrSubType::ULE:
        if (negated) {
          new_val =
              b.build_intrinsic(icmp->args[0], fir::IntrinsicSubType::Abs);
          break;
        }
        break;
      default:
        fmt::println("{:cd}", icmp);
        fmt::println("{:cd}", instr);
        TODO("okak");
        break;
    }
    if (!new_val.is_invalid()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(new_val);
      instr.destroy();
      return true;
    }
  }
  return false;
}

bool select_to_fabs(fir::Instr instr, WorkList &worklist) {
  auto icmp = instr->args[0].as_instr();
  bool negated = false;
  bool positive = false;
  // fmt::println("{:cd}", icmp);
  // fmt::println("{:cd}", instr);
  if (icmp->args[0] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::UnaryInstrSubType::FloatNeg) &&
      instr->args[2].as_instr()->args[0] == icmp->args[0]) {
    positive = true;
  }
  if (icmp->args[0] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::BinaryInstrSubType::FloatSub) &&
      is_constant_float_zero(instr->args[2].as_instr()->args[0]) &&
      instr->args[2].as_instr()->args[1] == icmp->args[0]) {
    positive = true;
  }
  if (icmp->args[0] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::UnaryInstrSubType::FloatNeg) &&
      instr->args[1].as_instr()->args[0] == icmp->args[0]) {
    negated = true;
  }
  if (icmp->args[0] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::BinaryInstrSubType::FloatSub) &&
      is_constant_float_zero(instr->args[1].as_instr()->args[0]) &&
      instr->args[1].as_instr()->args[1] == icmp->args[0]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::UnaryInstrSubType::FloatNeg) &&
      instr->args[2].as_instr()->args[0] == icmp->args[1]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[1] && instr->args[2].is_instr() &&
      instr->args[2].as_instr()->is(fir::BinaryInstrSubType::FloatSub) &&
      is_constant_float_zero(instr->args[2].as_instr()->args[0]) &&
      instr->args[2].as_instr()->args[1] == icmp->args[1]) {
    negated = true;
  }
  if (icmp->args[1] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::UnaryInstrSubType::FloatNeg) &&
      instr->args[1].as_instr()->args[0] == icmp->args[1]) {
    positive = true;
  }
  if (icmp->args[1] == instr->args[2] && instr->args[1].is_instr() &&
      instr->args[1].as_instr()->is(fir::BinaryInstrSubType::FloatSub) &&
      is_constant_float_zero(instr->args[1].as_instr()->args[0]) &&
      instr->args[1].as_instr()->args[1] == icmp->args[1]) {
    positive = true;
  }
  ASSERT(!negated || !positive);
  if (negated || positive) {
    fir::Builder b{instr};
    fir::ValueR new_val;
    switch ((fir::FCmpInstrSubType)icmp->subtype) {
      case fir::FCmpInstrSubType::OGE:
      case fir::FCmpInstrSubType::UGT:
      case fir::FCmpInstrSubType::UGE:
      case fir::FCmpInstrSubType::OGT:
        if (positive) {
          new_val =
              b.build_intrinsic(icmp->args[0], fir::IntrinsicSubType::FAbs);
          break;
        }
        break;
      case fir::FCmpInstrSubType::ULT:
      case fir::FCmpInstrSubType::ULE:
      case fir::FCmpInstrSubType::OLT:
      case fir::FCmpInstrSubType::OLE:
        if (negated) {
          new_val =
              b.build_intrinsic(icmp->args[0], fir::IntrinsicSubType::FAbs);
          break;
        }
        break;
      case fir::FCmpInstrSubType::OEQ:
      case fir::FCmpInstrSubType::ONE:
      case fir::FCmpInstrSubType::UEQ:
      case fir::FCmpInstrSubType::UNE:
      default:
        fmt::println("{:cd}", icmp);
        fmt::println("{:cd}", instr);
        TODO("okak");
        break;
    }
    if (!new_val.is_invalid()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(new_val);
      instr.destroy();
      return true;
    }
  }
  return false;
}

bool simplify_select(fir::Instr instr, fir::BasicBlock /*bb*/,
                     fir::Context & /*ctx*/, WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifySelect");
  }
  if (instr->args[0].is_constant()) {
    auto v1c = instr->args[0].as_constant();
    auto v1 = v1c->is_poison() ? 0 : v1c->as_int();
    push_all_uses(worklist, instr);
    if (v1 == 0) {
      instr->replace_all_uses(instr->args[2]);
    } else {
      instr->replace_all_uses(instr->args[1]);
    }
    instr.destroy();
    return true;
  }
  if (instr->args[1] == instr->args[2]) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[1]);
    instr.destroy();
    return true;
  }
  if (instr->args[1].is_constant() && instr->args[2].is_constant()) {
    auto arg1C = instr->args[1].as_constant();
    auto arg2C = instr->args[2].as_constant();
    if (arg1C->is_int() && arg2C->is_int()) {
      auto arg1 = arg1C->as_int();
      auto arg2 = arg2C->as_int();
      auto output_width = instr->get_type()->get_bitwidth();
      if ((arg1 == 1 || (output_width == 1 && arg1 != 0)) && arg2 == 0) {
        auto new_val = instr->args[0];
        if (output_width != 1) {
          fir::Builder b(instr);
          new_val = b.build_zext(new_val, instr->get_type());
        }
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_val);
        instr.destroy();
        return true;
      }
      if (arg1 == 0 && (arg2 == 1 || (output_width == 1 && arg2 != 0))) {
        fir::Builder b(instr);
        auto new_val =
            b.build_unary_op(instr->args[0], fir::UnaryInstrSubType::Not);
        if (output_width != 1) {
          new_val = b.build_zext(new_val, instr->get_type());
        }
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_val);
        instr.destroy();
        return true;
      }
    }
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ICmp) &&
      is_constant_int_zero(instr->args[0].as_instr()->args[1])) {
    if (select_to_abs(instr, worklist)) {
      return true;
    }
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::FCmp) &&
      is_constant_float_zero(instr->args[0].as_instr()->args[1])) {
    if (select_to_fabs(instr, worklist)) {
      return true;
    }
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ICmp)) {
    auto icmp = instr->args[0].as_instr();
    bool negated = false;
    bool positive = false;
    if (icmp->args[1] == instr->args[1] && icmp->args[0] == instr->args[2]) {
      positive = true;
    } else if (icmp->args[1] == instr->args[2] &&
               icmp->args[0] == instr->args[1]) {
      negated = true;
    }
    if (negated || positive) {
      fir::Builder b{instr};
      fir::ValueR new_val;
      switch ((fir::ICmpInstrSubType)icmp->subtype) {
        case fir::ICmpInstrSubType::UGT:
        case fir::ICmpInstrSubType::UGE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::UMax
                                              : fir::IntrinsicSubType::UMin);
          break;
        case fir::ICmpInstrSubType::ULT:
        case fir::ICmpInstrSubType::ULE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::UMin
                                              : fir::IntrinsicSubType::UMax);
          break;
        case fir::ICmpInstrSubType::SLT:
        case fir::ICmpInstrSubType::SLE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::SMin
                                              : fir::IntrinsicSubType::SMax);
          break;
        case fir::ICmpInstrSubType::SGT:
        case fir::ICmpInstrSubType::SGE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::SMax
                                              : fir::IntrinsicSubType::SMin);
          break;
        case fir::ICmpInstrSubType::EQ:
          if (positive && !icmp->args[0].is_constant()) {
            new_val = icmp->args[0];
            break;
          }
          break;
        case fir::ICmpInstrSubType::NE:
          if (negated && !icmp->args[0].is_constant()) {
            new_val = icmp->args[0];
            break;
          }
          break;
        default:
          fmt::println("{:cd}", icmp);
          fmt::println("{:cd}", instr);
          fmt::println("POS:{}  NEG:{}", positive, negated);
          TODO("okak");
          break;
      }
      if (!new_val.is_invalid()) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_val);
        instr.destroy();
        return true;
      }
    }
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::FCmp)) {
    auto fcmp = instr->args[0].as_instr();
    bool negated = false;
    bool positive = false;
    if (fcmp->args[1] == instr->args[1] && fcmp->args[0] == instr->args[2]) {
      positive = true;
    } else if (fcmp->args[1] == instr->args[2] &&
               fcmp->args[0] == instr->args[1]) {
      negated = true;
    }
    if (negated || positive) {
      fir::Builder b{instr};
      fir::ValueR new_val;
      switch ((fir::FCmpInstrSubType)fcmp->subtype) {
        // TODO(CORRECTNESS): idk  if both unordered and ordered can just be
        // converted
        case fir::FCmpInstrSubType::OGT:
        case fir::FCmpInstrSubType::UGT:
        case fir::FCmpInstrSubType::OGE:
        case fir::FCmpInstrSubType::UGE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::FMax
                                              : fir::IntrinsicSubType::FMin);
          break;
        case fir::FCmpInstrSubType::ULT:
        case fir::FCmpInstrSubType::OLT:
        case fir::FCmpInstrSubType::OLE:
        case fir::FCmpInstrSubType::ULE:
          new_val = b.build_intrinsic(instr->args[1], instr->args[2],
                                      negated ? fir::IntrinsicSubType::FMin
                                              : fir::IntrinsicSubType::FMax);
          break;
        case fir::FCmpInstrSubType::UEQ:
        case fir::FCmpInstrSubType::OEQ:
          if (positive && !fcmp->args[0].is_constant()) {
            new_val = fcmp->args[0];
            break;
          }
          break;
        case fir::FCmpInstrSubType::ONE:
        case fir::FCmpInstrSubType::UNE:
          if (negated && !fcmp->args[0].is_constant()) {
            new_val = fcmp->args[0];
            break;
          }
          break;
        default:
          fmt::println("{:cd}", fcmp);
          fmt::println("{:cd}", instr);
          TODO("okak");
      }
      if (!new_val.is_invalid()) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_val);
        instr.destroy();
        return true;
      }
    }
  }
  return false;
}
bool simplify_cond_branch(fir::Instr instr, fir::BasicBlock bb,
                          fir::Context & /*ctx*/, WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyCondBranch");
  }
  if (instr->args[0].is_constant()) {
    auto c = instr->args[0].as_constant();
    fir::Builder b(bb);
    b.at_end(bb);

    auto *target = instr->bbs.data();

    if (c->is_poison()) {
      // TODO: Could also pplace some unreach??
    } else {
      auto v1 = c->as_int();
      if (v1 == 0) {
        target = &instr->bbs[1];
      }
    }

    auto new_branch = b.build_branch(target->bb);
    for (auto old_arg : target->args) {
      new_branch.add_bb_arg(0, old_arg);
    }
    instr.remove_from_parent();
    return true;
  }
  if (instr->args[0].is_instr()) {
    auto cond = instr->args[0].as_instr();
    if (cond->is(fir::InstrType::UnaryInstr) &&
        cond->subtype == (u32)fir::UnaryInstrSubType::Not) {
      TVec<fir::ValueR> args0 = {instr->bbs[0].args.begin(),
                                 instr->bbs[0].args.end()};
      TVec<fir::ValueR> args1 = {instr->bbs[1].args.begin(),
                                 instr->bbs[1].args.end()};
      fir::BasicBlock bb0 = instr->bbs[0].bb;
      fir::BasicBlock bb1 = instr->bbs[1].bb;
      instr.clear_bb_args(0);
      instr.clear_bb_args(1);

      instr.replace_bb(0, bb1, true, false);
      for (auto arg : args1) {
        instr.add_bb_arg(0, arg);
      }
      instr.replace_bb(1, bb0, true, false);
      for (auto arg : args0) {
        instr.add_bb_arg(1, arg);
      }
      instr.replace_arg(0, cond->args[0]);
      worklist.push_back({instr, instr->get_parent()});
      return true;
    }
  }
  return false;
}

void simplify_switch_branch(fir::Instr instr, fir::BasicBlock bb,
                            fir::Context & /*ctx*/, WorkList & /*worklist*/) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifySwitch");
  }
  if (!instr->args.empty() && instr->args.back().is_constant()) {
    fir::Builder b(bb);
    b.at_end(bb);

    bool might_be_found = false;
    fir::BBRefWithArgs *target = nullptr;
    auto v1 = instr->args.back().as_constant();
    for (size_t arg_id = 0; arg_id < instr->args.size() - 1; arg_id++) {
      if (!instr->args[arg_id].is_constant()) {
        might_be_found = true;
        continue;
      }
      auto argC = instr->args[arg_id].as_constant();
      if (argC->eql(*v1.operator->())) {
        target = &instr->bbs[arg_id];
        break;
      }
    }

    if (!might_be_found && nullptr == target &&
        instr->bbs.size() == instr->args.size()) {
      // use default case then;
      target = &instr->bbs.back();
    }

    if (target != nullptr) {
      auto new_branch = b.build_branch(target->bb);
      for (auto old_arg : target->args) {
        new_branch.add_bb_arg(0, old_arg);
      }
      instr.remove_from_parent();
      return;
    }
  }

  // if theres any bb+arg that is the same as default forward it to it
  if (instr->bbs.size() == instr->args.size()) {
    fir::Builder b(bb);
    b.at_end(bb);

    for (size_t ip1 = instr->args.size() - 1; ip1 > 0; ip1--) {
      auto &default_target = instr->bbs.back();
      size_t arg_idx = ip1 - 1;
      auto &curr_bb = instr->bbs[arg_idx];
      if (default_target.bb != curr_bb.bb) {
        continue;
      }
      bool found = true;
      ASSERT(curr_bb.args.size() == default_target.args.size());
      for (size_t i = 0; i < curr_bb.args.size(); i++) {
        if (curr_bb.args[i] != default_target.args[i]) {
          found = false;
          break;
        }
      }
      if (found) {
        instr.remove_arg(arg_idx);
        instr.remove_bb(arg_idx);
      }
    }
  }
}

bool simplify_extend(fir::Instr instr, fir::BasicBlock /*bb*/,
                     fir::Context &ctx, WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyExtend");
  }
  // TODO: could also maybe figure out cases where we can convert everything
  // into higher bitwidth
  (void)ctx;
  auto iszext = instr->is(fir::InstrType::ZExt);
  if (instr->args[0].get_type() == instr.get_type()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[0]);
    instr.destroy();
    return true;
  }
  if (instr->args[0].is_constant() &&
      instr->args[0].as_constant()->is_poison()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(
        fir::ValueR{ctx->get_poisson_value(instr->get_type())});
    instr.destroy();
    return true;
  }
  if (instr->args[0].is_constant() && instr->args[0].as_constant()->is_int()) {
    auto c = instr->args[0].as_constant()->as_int();
    push_all_uses(worklist, instr);
    if (iszext) {
      auto mask = ((u128)1 << instr->args[0].get_type()->get_bitwidth()) - 1;
      instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(
          std::bit_cast<i128>(c & mask), instr->get_type())});
    } else {
      instr->replace_all_uses(fir::ValueR{
          ctx->get_constant_value(std::bit_cast<i128>(c), instr->get_type())});
    }
    instr.destroy();
    return true;
  }
  if (instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::VectorISubType::Broadcast)) {
    auto arg = instr->args[0].as_instr();
    auto res_subty = ctx->get_int_type(instr->get_type()->as_vec().bitwidth);
    fir::Builder b(instr);
    fir::ValueR extended;
    if (iszext) {
      extended = b.build_zext(arg->args[0], res_subty);
    } else {
      extended = b.build_sext(arg->args[0], res_subty);
    }
    auto new_res = b.build_vbroadcast(extended, instr->get_type());
    push_all_uses(worklist, instr);
    instr->replace_all_uses(new_res);
    if (arg->get_n_uses() == 1) {
      arg.destroy();
    }
    instr.destroy();
    return true;
  }
  if (iszext && instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ITrunc)) {
    // a -> tr -> b
    auto trunc = instr->args[0].as_instr();
    auto input_ty = trunc->args[0].get_type();
    auto input_width = input_ty->get_bitwidth();
    auto output_type = instr.get_type();
    auto output_width = output_type->get_bitwidth();
    fir::Builder b(instr);
    auto bitwidth = trunc.get_type()->get_size() * 8;
    auto mask = ctx->get_constant_value(((u64)1 << bitwidth) - 1, input_ty);
    auto new_result = b.build_binary_op(trunc->args[0], fir::ValueR{mask},
                                        fir::BinaryInstrSubType::And);
    push_all_uses(worklist, instr);
    if (input_width == output_width) {
      instr->replace_all_uses(new_result);
    } else if (input_width < output_width) {
      auto end_res = b.build_zext(new_result, output_type);
      instr->replace_all_uses(end_res);
    } else if (input_width > output_width) {
      auto end_res = b.build_itrunc(new_result, output_type);
      instr->replace_all_uses(end_res);
    }
    instr.destroy();
    return true;
  }
  if ((instr->is(fir::InstrType::SExt) || iszext) &&
      instr->args[0].is_instr()) {
    auto argi = instr->args[0].as_instr();
    if (argi->is(fir::InstrType::ZExt)) {
      // just to make sure incase that didnt get cleaned up yet
      if (argi->args[0].get_type() != argi.get_type()) {
        fir::Builder b(instr);
        auto new_result = b.build_zext(argi->args[0], instr->get_type());
        push_all_uses(worklist, instr);
        instr->replace_all_uses(new_result);
        instr.destroy();
        return true;
      }
    }
    auto is_non_overflowing = (argi->NSW && !iszext) || (argi->NUW && iszext);
    if (argi->is(fir::BinaryInstrSubType::And) ||
        argi->is(fir::BinaryInstrSubType::Or) ||
        argi->is(fir::BinaryInstrSubType::Xor) ||
        (argi->is(fir::BinaryInstrSubType::IntAdd) && is_non_overflowing) ||
        (argi->is(fir::BinaryInstrSubType::IntMul) && is_non_overflowing) ||
        (iszext && argi->is(fir::BinaryInstrSubType::Shr))) {
      fir::Builder buh{argi};
      auto ext_ty = instr->get_type();
      fir::ValueR arg1Ext;
      fir::ValueR arg2Ext;
      if (iszext) {
        arg1Ext = buh.build_zext(argi->args[0], ext_ty);
        arg2Ext = buh.build_zext(argi->args[1], ext_ty);
      } else {
        arg1Ext = buh.build_sext(argi->args[0], ext_ty);
        arg2Ext = buh.build_sext(argi->args[1], ext_ty);
      }
      auto res = buh.build_binary_op(
          arg1Ext, arg2Ext, (fir::BinaryInstrSubType)argi->get_instr_subtype());
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return true;
    }
  }
  if (instr->is(fir::InstrType::SExt) && instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::SExt)) {
    auto sext = instr->args[0].as_instr();
    fir::Builder b(instr);
    auto new_result = b.build_sext(sext->args[0], instr->get_type());
    push_all_uses(worklist, instr);
    instr->replace_all_uses(new_result);
    instr.destroy();
    return true;
  }
  return false;
}

bool simplify_itrunc(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                     WorkList &worklist, AttributerManager &man) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyITrunc");
  }
  (void)instr;
  (void)worklist;
  // ext
  // +-*
  // itrunc
  // = +-* on smaller bitwidth
  // since lea doesnt like 1byte value we skip those for now
  auto old_bitwidth = instr->args[0].get_type()->get_bitwidth();
  auto new_bitwidth = instr.get_type()->get_bitwidth();
  auto mask = ((i128)1 << new_bitwidth) - 1;

  if (instr->args[0].get_type() == instr.get_type()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(instr->args[0]);
    instr.destroy();
    return true;
  }
  if (instr->args[0].is_constant()) {
    auto c = instr->args[0].as_constant();
    if (c->is_poison()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(
          fir::ValueR{ctx->get_poisson_value(instr->get_type())});
      instr.destroy();
      return true;
    }
    auto v = c->as_int();

    ASSERT(new_bitwidth < old_bitwidth);

    auto truncated_v = v & mask;
    auto new_v = ctx->get_constant_int(truncated_v, new_bitwidth);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(fir::ValueR{new_v});
    instr.destroy();
    return true;
  }
  if (instr->args[0].is_bb_arg()) {
    auto arg = instr->args[0].as_bb_arg();
    auto origin_bb = arg->get_parent();
    // cant be entry basic block
    if (origin_bb->get_parent().func->basic_blocks[0] != origin_bb) {
      // fmt::println("Got hit {}", arg->get_n_uses());
      // for (auto &u : arg->uses) {
      //   fmt::println("  {:cd}", u.user);
      // }
      // if we have a bbarg as argument and its only used once
      //  it prob makes sense to just propagate the itrunc through the
      //  bb arg in the hope it optimizes better before that
      // this could also backfire (should be safe if we also propagate the
      // sext) but if the itrunc gets multiple uses it wont be propagated
      // and we got issues then
      if (arg->get_n_uses() == 1) {
        (void)bb;
        // fmt::println("{:cd}", origin_bb);
        // fmt::println("{:cd}", bb);
        auto arg_id = origin_bb->get_arg_id(arg);
        for (auto &u : origin_bb->uses) {
          auto user = u.user;
          fir::Builder buh{user};
          auto new_trunc_val = buh.build_itrunc(user->bbs[u.argId].args[arg_id],
                                                instr->get_type());
          user.replace_bb_arg(u.argId, arg_id, new_trunc_val);
          // fmt::println("{:cd}", user->get_parent());
        }
        arg->_type = instr.get_type();
        instr->replace_all_uses(fir::ValueR{arg});
        instr.destroy();
        // fmt::println("{:cd}", origin_bb);
        // fmt::println("{:cd}", bb);
        // TODO("okak");
        return true;
      }
    }
  }
  if (instr->args[0].is_instr()) {
    auto arg_i = instr->args[0].as_instr();
    auto out_type = instr->get_type();
    (void)out_type;

    if ((arg_i->is(fir::InstrType::SExt) || arg_i->is(fir::InstrType::ZExt)) &&
        arg_i->args[0].get_type() == instr.get_type()) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(arg_i->args[0]);
      instr.destroy();
      return true;
    }

    switch (arg_i->instr_type) {
      case fir::InstrType::BinaryInstr: {
        auto i_arg0 = arg_i->args[0];
        auto i_arg1 = arg_i->args[1];
        fir::Builder b{instr};
        switch ((fir::BinaryInstrSubType)arg_i->subtype) {
          case fir::BinaryInstrSubType::Shl: {
            if (i_arg1.is_constant()) {
              auto i_arg1c = i_arg1.as_constant()->as_int();
              if (i_arg1c >= new_bitwidth) {
                push_all_uses(worklist, instr);
                instr->replace_all_uses(fir::ValueR{
                    ctx->get_constant_value((i128)0, instr->get_type())});
                instr.destroy();
                return true;
              }
              if (i_arg1c >= 0 && i_arg1c < ((i128)1 << new_bitwidth)) {
                push_all_uses(worklist, instr);
                auto v0 = b.build_itrunc(i_arg0, out_type);
                auto v1 = b.build_itrunc(i_arg1, out_type);
                auto r = b.build_binary_op(
                    v0, v1, (fir::BinaryInstrSubType)arg_i->subtype);
                push_all_uses(worklist, instr);
                worklist.push_back(
                    {v0.as_instr(), v0.as_instr()->get_parent()});
                worklist.push_back(
                    {v1.as_instr(), v1.as_instr()->get_parent()});
                instr->replace_all_uses(r);
                instr.destroy();
                return true;
              }
            }
          } break;
          case fir::BinaryInstrSubType::Xor:
          case fir::BinaryInstrSubType::And:
          case fir::BinaryInstrSubType::Or:
          case fir::BinaryInstrSubType::IntSub:
          case fir::BinaryInstrSubType::IntMul:
          case fir::BinaryInstrSubType::IntAdd: {
            auto v0 = b.build_itrunc(i_arg0, out_type);
            auto v1 = b.build_itrunc(i_arg1, out_type);
            auto r = b.build_binary_op(v0, v1,
                                       (fir::BinaryInstrSubType)arg_i->subtype);
            worklist.push_back({v0.as_instr(), v0.as_instr()->get_parent()});
            worklist.push_back({v1.as_instr(), v1.as_instr()->get_parent()});
            push_all_uses(worklist, r.as_instr());
            instr->replace_all_uses(r);
            instr.destroy();
            return true;
          }
          default:
            break;
        }
        break;
      }
      default:
        break;
    }
  }

  auto *arg_known_bits = man.get_or_create_analysis<KnownBits>(instr->args[0]);
  man.run(ctx);
  if (((arg_known_bits->known_one ^ arg_known_bits->known_zero) & mask) ==
      mask) {
    auto val =
        ctx->get_constant_int((arg_known_bits->known_one & mask), new_bitwidth);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(fir::ValueR{val});
    instr.destroy();
    return true;
  }
  return false;
}

bool simplify_unary(fir::Instr instr, fir::BasicBlock /*bb*/, fir::Context &ctx,
                    WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyUnary");
  }
  if (instr->args[0].is_constant() &&
      instr->args[0].as_constant()->is_poison()) {
    push_all_uses(worklist, instr);
    instr->replace_all_uses(
        fir::ValueR{ctx->get_poisson_value(instr.get_type())});
    instr.destroy();
    return true;
  }
  if ((fir::UnaryInstrSubType)instr->subtype ==
          fir::UnaryInstrSubType::IntNeg &&
      instr->get_type()->get_bitwidth() == 1) {
    instr->subtype = (u32)fir::UnaryInstrSubType::Not;
    worklist.push_back(
        InstSimplify::WorkItem{.instr = instr, .b = instr->get_parent()});
    return true;
  }
  if ((fir::UnaryInstrSubType)instr->subtype == fir::UnaryInstrSubType::Not &&
      instr->args[0].is_instr()) {
    auto input_instr = instr->args[0].as_instr();
    if (input_instr->is(fir::UnaryInstrSubType::Not)) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(input_instr->args[0]);
      instr.destroy();
      return true;
    }
  }
  if ((fir::UnaryInstrSubType)instr->subtype == fir::UnaryInstrSubType::Not &&
      instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::ICmp)) {
    push_all_uses(worklist, instr);
    auto old_icmp = instr->args[0].as_instr();
    auto new_subtype = fir::ICmpInstrSubType::INVALID;
    switch ((fir::ICmpInstrSubType)old_icmp->subtype) {
      case fir::ICmpInstrSubType::INVALID:
        break;
      case fir::ICmpInstrSubType::ULT:
        new_subtype = fir::ICmpInstrSubType::UGE;
        break;
      case fir::ICmpInstrSubType::SLT:
        new_subtype = fir::ICmpInstrSubType::SGE;
        break;
      case fir::ICmpInstrSubType::NE:
        new_subtype = fir::ICmpInstrSubType::EQ;
        break;
      case fir::ICmpInstrSubType::EQ:
        new_subtype = fir::ICmpInstrSubType::NE;
        break;
      case fir::ICmpInstrSubType::SGT:
        new_subtype = fir::ICmpInstrSubType::SLE;
        break;
      case fir::ICmpInstrSubType::UGT:
        new_subtype = fir::ICmpInstrSubType::ULE;
        break;
      case fir::ICmpInstrSubType::UGE:
        new_subtype = fir::ICmpInstrSubType::ULT;
        break;
      case fir::ICmpInstrSubType::ULE:
        new_subtype = fir::ICmpInstrSubType::UGT;
        break;
      case fir::ICmpInstrSubType::SGE:
        new_subtype = fir::ICmpInstrSubType::SLT;
        break;
      case fir::ICmpInstrSubType::SLE:
        new_subtype = fir::ICmpInstrSubType::SGT;
        break;
      case fir::ICmpInstrSubType::MulOverflow:
      case fir::ICmpInstrSubType::AddOverflow:
        // TODO: impl
        return true;
    }
    ASSERT(new_subtype != fir::ICmpInstrSubType::INVALID);

    auto bb = fir::Builder{instr};
    auto new_comp =
        bb.build_int_cmp(old_icmp->args[0], old_icmp->args[1], new_subtype);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(new_comp);
    instr.destroy();
    return true;
  }
  if ((fir::UnaryInstrSubType)instr->subtype == fir::UnaryInstrSubType::Not &&
      instr->args[0].is_instr() &&
      instr->args[0].as_instr()->is(fir::InstrType::FCmp)) {
    push_all_uses(worklist, instr);
    auto old_fcmp = instr->args[0].as_instr();
    auto new_subtype = fir::FCmpInstrSubType::INVALID;
    switch ((fir::FCmpInstrSubType)old_fcmp->subtype) {
      case fir::FCmpInstrSubType::OEQ:
        new_subtype = fir::FCmpInstrSubType::ONE;
        break;
      case fir::FCmpInstrSubType::OGT:
        new_subtype = fir::FCmpInstrSubType::OLE;
        break;
      case fir::FCmpInstrSubType::OGE:
        new_subtype = fir::FCmpInstrSubType::OLT;
        break;
      case fir::FCmpInstrSubType::OLT:
        new_subtype = fir::FCmpInstrSubType::OGE;
        break;
      case fir::FCmpInstrSubType::OLE:
        new_subtype = fir::FCmpInstrSubType::OGT;
        break;
      case fir::FCmpInstrSubType::ONE:
        new_subtype = fir::FCmpInstrSubType::OEQ;
        break;
      case fir::FCmpInstrSubType::UEQ:
        new_subtype = fir::FCmpInstrSubType::UNE;
        break;
      case fir::FCmpInstrSubType::UGT:
        new_subtype = fir::FCmpInstrSubType::ULE;
        break;
      case fir::FCmpInstrSubType::UGE:
        new_subtype = fir::FCmpInstrSubType::ULT;
        break;
      case fir::FCmpInstrSubType::ULT:
        new_subtype = fir::FCmpInstrSubType::UGE;
        break;
      case fir::FCmpInstrSubType::ULE:
        new_subtype = fir::FCmpInstrSubType::UGT;
        break;
      case fir::FCmpInstrSubType::UNE:
        new_subtype = fir::FCmpInstrSubType::UEQ;
        break;
      case fir::FCmpInstrSubType::AlwFalse:
        new_subtype = fir::FCmpInstrSubType::AlwTrue;
        break;
      case fir::FCmpInstrSubType::AlwTrue:
        new_subtype = fir::FCmpInstrSubType::AlwFalse;
        break;
      case fir::FCmpInstrSubType::ORD:
      case fir::FCmpInstrSubType::UNO:
      case fir::FCmpInstrSubType::IsNaN:
      case fir::FCmpInstrSubType::INVALID:
        break;
    }
    ASSERT(new_subtype != fir::FCmpInstrSubType::INVALID);

    auto bb = fir::Builder{instr};
    auto new_comp =
        bb.build_float_cmp(old_fcmp->args[0], old_fcmp->args[1], new_subtype);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(new_comp);
    instr.destroy();
    return true;
  }
  if (instr->args[0].is_constant()) {
    if (instr->is(fir::UnaryInstrSubType::Not)) {
      push_all_uses(worklist, instr);
      auto out_type = instr.get_type();
      ASSERT(out_type->is_int());
      auto width = out_type->as_int();
      auto shift_amount = 128 - width;
      auto v = (~instr->args[0].as_constant()->as_int());
      auto res_v = (v << shift_amount >> shift_amount);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(
          fir::ValueR{ctx->get_constant_value(res_v, out_type)});
      instr.destroy();
      return true;
    }
  }
  return false;
}

bool simplify_conversion(fir::Instr instr, fir::BasicBlock /*bb*/,
                         fir::Context &ctx, WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyConversion");
  }
  (void)ctx;
  (void)worklist;
  switch ((fir::ConversionSubType)instr->subtype) {
    case fir::ConversionSubType::INVALID:
      TODO("unreach");
    case fir::ConversionSubType::BitCast:
      if (instr->args[0].get_type()->is_int() && instr->get_type()->is_ptr()) {
        fir::Builder buh{instr};
        auto r = buh.build_conversion_op(instr->args[0], instr.get_type(),
                                         fir::ConversionSubType::IntToPtr);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(r);
        instr.destroy();
        return true;
      }
      if (instr->get_type()->is_int() && instr->args[0].get_type()->is_ptr()) {
        fir::Builder buh{instr};
        auto r = buh.build_conversion_op(instr->args[0], instr.get_type(),
                                         fir::ConversionSubType::PtrToInt);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(r);
        instr.destroy();
        return true;
      }
      if (instr->args[0].get_type() == instr->get_type()) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return true;
      }
      if (instr->args[0].is_instr()) {
        auto a0 = instr->args[0].as_instr();
        if (a0->is(fir::InstrType::LoadInstr) &&
            load_into_conversion_simpl(instr, a0, worklist)) {
          return true;
        }
        if (a0->is(fir::ConversionSubType::BitCast) &&
            a0->args[0].get_type() == instr.get_type()) {
          push_all_uses(worklist, instr);
          instr->replace_all_uses(a0->args[0]);
          instr.destroy();
          return true;
        }
        if ((a0->is(fir::BinaryInstrSubType::And) ||
             a0->is(fir::BinaryInstrSubType::Or) ||
             a0->is(fir::BinaryInstrSubType::Xor))) {
          auto arg0 = a0->args[0];
          auto arg1 = a0->args[1];
          bool arg0_right =
              arg0.is_constant() ||
              (arg0.is_instr() &&
               arg0.as_instr()->is(fir::ConversionSubType::BitCast));
          bool arg1_right =
              arg1.is_constant() ||
              (arg1.is_instr() &&
               arg1.as_instr()->is(fir::ConversionSubType::BitCast));
          if (arg0_right && arg1_right) {
            fir::Builder buh{instr};
            push_all_uses(worklist, instr);
            auto arg0_casted = arg0;
            auto arg1_casted = arg1;
            if (arg0.is_constant()) {
              auto r =
                  arg0_casted.as_constant()->bit_cast(ctx, instr.get_type());
              ASSERT(r.has_value());
              arg0_casted = fir::ValueR{r.value()};
            } else {
              arg0_casted = arg0.as_instr()->args[0];
            }
            if (arg1.is_constant()) {
              auto r =
                  arg1_casted.as_constant()->bit_cast(ctx, instr.get_type());
              ASSERT(r.has_value());
              arg1_casted = fir::ValueR{r.value()};
            } else {
              arg1_casted = arg1.as_instr()->args[0];
            }
            auto r =
                buh.build_binary_op(arg0_casted, arg1_casted, instr.get_type(),
                                    (fir::BinaryInstrSubType)a0->subtype);
            instr->replace_all_uses(r);
            instr.destroy();
            if (a0->get_n_uses() == 0) {
              a0.destroy();
            }
            if (arg0.is_instr() && arg0.as_instr()->get_n_uses() == 0) {
              arg0.as_instr().destroy();
            }
            if (arg1.is_instr() && arg1.as_instr()->get_n_uses() == 0) {
              arg1.as_instr().destroy();
            }
            return true;
          }
        }
      }
      if (instr->args[0].is_constant()) {
        auto r = instr->args[0].as_constant()->bit_cast(ctx, instr->get_type());
        ASSERT(r.has_value());
        instr->replace_all_uses(fir::ValueR{r.value()});
        return true;
      }
      break;
    case fir::ConversionSubType::SITOFP:
    case fir::ConversionSubType::UITOFP:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_int()) {
        auto val = instr->args[0].as_constant()->as_int();
        push_all_uses(worklist, instr);
        auto out_width = instr->get_type()->as_float();
        if (out_width == 32) {
          instr->replace_all_uses(fir::ValueR{
              ctx->get_constant_value((f32)val, instr->get_type())});
        } else if (out_width == 64) {
          instr->replace_all_uses(fir::ValueR{
              ctx->get_constant_value((f64)val, instr->get_type())});
        } else {
          TODO("Not supported other float bitwidths");
        }
        instr.destroy();
        return true;
      }
      break;
    case fir::ConversionSubType::PtrToInt:
      if (instr->args[0].is_constant() || instr->args[0].get_type()->is_int()) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return true;
      }
      if (instr->args[0].is_instr()) {
        auto a0 = instr->args[0].as_instr();
        if (a0->is(fir::InstrType::LoadInstr) &&
            load_into_conversion_simpl(instr, a0, worklist)) {
          return true;
        }
        if (a0->is(fir::ConversionSubType::IntToPtr)) {
          push_all_uses(worklist, instr);
          instr->replace_all_uses(a0->args[0]);
          instr.destroy();
          return true;
        }
      }
      break;
    case fir::ConversionSubType::IntToPtr:
      if (instr->args[0].is_constant() || instr->args[0].get_type()->is_ptr()) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return true;
      }
      if (instr->args[0].is_instr()) {
        auto a0 = instr->args[0].as_instr();
        if (a0->is(fir::InstrType::LoadInstr) &&
            load_into_conversion_simpl(instr, a0, worklist)) {
          return true;
        }
        if (a0->is(fir::ConversionSubType::PtrToInt)) {
          push_all_uses(worklist, instr);
          instr->replace_all_uses(a0->args[0]);
          instr.destroy();
          return true;
        }
      }
      break;
    case fir::ConversionSubType::FPTOSI:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_float()) {
        auto val = instr->args[0].as_constant()->as_float();
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value((i128)val, instr->get_type())});
        instr.destroy();
        // TODO("OKAK IMPL");
        return true;
      }
      break;
    case fir::ConversionSubType::FPTOUI:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_float()) {
        auto val = instr->args[0].as_constant()->as_float();
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value((u64)val, instr->get_type())});
        instr.destroy();
        return true;
      }
      break;
    case fir::ConversionSubType::FPEXT:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_float()) {
        ASSERT(instr->args[0].get_type()->as_float() == 32);
        ASSERT(instr.get_type()->as_float() == 64);
        auto val = instr->args[0].as_constant()->as_f32();
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value((f64)val, instr->get_type())});
        instr.destroy();
        return true;
      }
      break;
    case fir::ConversionSubType::FPTRUNC:
      if (instr->args[0].is_constant() &&
          instr->args[0].as_constant()->is_float()) {
        ASSERT(instr->args[0].get_type()->as_float() == 64);
        ASSERT(instr.get_type()->as_float() == 32);
        auto val = instr->args[0].as_constant()->as_f64();
        push_all_uses(worklist, instr);
        instr->replace_all_uses(
            fir::ValueR{ctx->get_constant_value((f32)val, instr->get_type())});
        instr.destroy();
        return true;
      }
      break;
  }
  return false;
}

bool simplify_store(fir::Instr instr) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyStore");
  }
  if ((instr->args[0].is_constant() &&
       instr->args[0].as_constant()->is_poison()) ||
      (instr->args[1].is_constant() &&
       instr->args[1].as_constant()->is_poison())) {
    instr.destroy();
    return true;
  }
  if (instr->args[1].is_instr()) {
    auto val_i = instr->args[1].as_instr();
    if (val_i->is(fir::ConversionSubType::PtrToInt) ||
        val_i->is(fir::ConversionSubType::IntToPtr) ||
        val_i->is(fir::ConversionSubType::BitCast)) {
      if (instr->get_n_uses() == 1 || (!instr->Volatile && !instr->Atomic)) {
        fir::Builder buh{instr};
        buh.build_store(instr->args[0], val_i->args[0], instr->Atomic,
                        instr->Volatile);
        instr.destroy();
        if (val_i->get_n_uses() == 0) {
          val_i.destroy();
        }
        return true;
      }
    }
    // TODO: is this correct double check
    // if (val_i->is(fir::InstrType::InsertValue)) {
    //   fir::Builder buh{instr};
    //   // skip if its a insert(posion ...)
    //   if (!val_i->args[0].is_constant() ||
    //       !val_i->args[0].as_constant()->is_poison()) {
    //     buh.build_store(instr->args[0], val_i->args[0]);
    //   }
    //   auto stru_ty = val_i.get_type()->as_struct();
    //   auto *ctx = instr->get_parent()->get_parent()->ctx;
    //   auto offset = buh.build_int_add(
    //       instr->args[0],
    //       fir::ValueR{ctx->get_constant_int(
    //           stru_ty.elems[val_i->args[2].as_constant()->as_int()].offset,
    //           32)});
    //   buh.build_store(offset, val_i->args[1]);
    //   instr.destroy();
    //   if (val_i->get_n_uses() == 0) {
    //     val_i.destroy();
    //   }
    //   // fmt::println("{:cd}", buh.get_curr_bb());
    //   // TODO("okak");
    //   return true;
    // }
  }
  return false;
}

bool simplify_call(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                   WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyCall");
  }
  (void)bb;
  (void)ctx;
  (void)worklist;
  if (instr->args[0].is_constant() && instr->args[0].as_constant()->is_func()) {
    auto funci = instr->args[0].as_constant()->as_func();
    if (instr->get_n_uses() == 0 &&
        (funci->mem_read_none || funci->mem_read_only)) {
      instr.destroy();
      return true;
    }

    if (foptim::utils::assume_cstdlib_beheaviour) {
      // constant propagate strlen of string literals
      if (funci->name == "strlen" && instr->args[1].is_constant() &&
          instr->args[1].as_constant()->is_global()) {
        auto g = instr->args[1].as_constant()->as_global();
        if (g->is_constant && g->init_value != nullptr) {
          auto len = strlen((const char *)g->init_value);
          auto len_constant = ctx->get_constant_int(len, 64);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(fir::ValueR{len_constant});
          instr.destroy();
          return true;
        }
      }
      // void * __memset_chk(void * dest, int c, size_t len, size_t destlen);
      if (funci->name == "__memset_chk" && instr->args[3].is_constant() &&
          instr->args[4].is_constant() && instr->get_n_uses() == 0) {
        auto len = std::bit_cast<u128>(instr->args[3].as_constant()->as_int());
        auto destlen =
            std::bit_cast<u128>(instr->args[4].as_constant()->as_int());
        if (len <= destlen) {
          fir::generate_memset(ctx);
          fir::Builder buh{instr};
          auto new_c = buh.build_itrunc(instr->args[2], ctx->get_int_type(8));
          auto memset = ctx->get_function("foptim.memset");
          instr.replace_arg(0, fir::ValueR{ctx->get_constant_value(memset)});
          instr.replace_arg(2, new_c);
          instr.remove_arg(4);
          instr->value_type = ctx->get_void_type();
          instr->extra_type = memset->func_ty;
          return true;
        }
        TODO("emit unreach");
      }
      // void * __memcpy_chk(void * dest, const void * src, size_t len, size_t
      // destlen);
      if (funci->name == "__memcpy_chk" && instr->args[3].is_constant() &&
          instr->args[4].is_constant() && instr->get_n_uses() == 0) {
        auto len = std::bit_cast<u128>(instr->args[3].as_constant()->as_int());
        auto destlen =
            std::bit_cast<u128>(instr->args[4].as_constant()->as_int());
        if (len <= destlen) {
          fir::generate_memcpy(ctx);
          fir::Builder buh{instr};
          auto memcpy = ctx->get_function("foptim.memcpy");
          instr.replace_arg(0, fir::ValueR{ctx->get_constant_value(memcpy)});
          instr.remove_arg(4);
          instr->value_type = ctx->get_void_type();
          instr->extra_type = memcpy->func_ty;
          return true;
        }
        TODO("emit unreach");
      }

      // NOTE: this current version does not work cause it does not append
      //  printf(lit) -> fputs(lit, stdout)
      //  if (funci->name == "printf" && instr->args.size() == 2 &&
      //      instr->get_n_uses() == 0) {

      //   if (!ctx->has_function("write")) {
      //     auto puts_func = ctx->create_function(
      //         "write",
      //         ctx->get_func_ty(ctx->get_int_type(64),
      //                          {ctx->get_int_type(32),
      //                          ctx->get_ptr_type(),
      //                           ctx->get_int_type(64)}));
      //     puts_func->linkage = fir::Linkage::External;
      //     puts_func->basic_blocks.clear();
      //   }
      //   if (!ctx->has_function("strlen")) {
      //     auto strlen_func = ctx->create_function(
      //         "strlen",
      //         ctx->get_func_ty(ctx->get_int_type(64),
      //         {ctx->get_ptr_type()}));
      //     strlen_func->linkage = fir::Linkage::External;
      //     strlen_func->basic_blocks.clear();
      //   }

      //   // fir::ValueR stdout{};
      //   // if (!ctx->has_global("stdout")) {
      //   //   auto global = ctx->insert_global("stdout", 8);
      //   //   global->linkage = fir::Linkage::External;
      //   //   stdout = fir::ValueR{ctx->get_constant_value(global)};
      //   // } else {
      //   //   stdout =
      //   //
      //   fir::ValueR{ctx->get_constant_value(ctx->get_global("stdout"))};
      //   // }

      //   auto write_func = ctx->get_function("write");
      //   auto strlen_func = ctx->get_function("strlen");

      //   fir::Builder builder{instr};
      //   // TODO: should load stdcout
      //   //
      //   //  auto stdout_ptr_val = builder.build_load(ctx->get_ptr_type(),
      //   //  stdout); auto stdout_val =
      //   //      builder.build_load(ctx->get_ptr_type(), stdout_ptr_val);
      //   //  fir::ValueR args[2] = {instr->args[1],
      //   fir::ValueR{stdout_val}}; fir::ValueR args1[1] = {
      //       instr->args[1],
      //   };
      //   auto string_len = builder.build_call(
      //       fir::ValueR{ctx->get_constant_value(strlen_func)},
      //       write_func->func_ty, ctx->get_int_type(32), args1);
      //   fir::ValueR args2[3] = {
      //       fir::ValueR{ctx->get_constant_int(1, 32)},
      //       instr->args[1],
      //       string_len,
      //   };
      //   builder.build_call(fir::ValueR{ctx->get_constant_value(write_func)},
      //                      write_func->func_ty, ctx->get_int_type(32),
      //                      args2);
      //   instr.destroy();
      //   // TODO("okak");
      //   return;
      // }
    }
  }
  return false;
}

bool simplify_load(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                   WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyLoad");
  }
  (void)bb;
  (void)ctx;
  (void)worklist;
  if (instr->args[0].is_constant() && !instr->Volatile) {
    auto arg0_const = instr->args[0].as_constant();
    if (arg0_const->is_poison() || arg0_const->is_null() ||
        (arg0_const->is_int() && arg0_const->as_int() <= 0)) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(
          fir::ValueR{ctx->get_poisson_value(instr->get_type())});
      instr.destroy();
      return true;
    }
    // if (arg0_const->is_null() ||
    //     (arg0_const->is_int() && arg0_const->as_int() == 0)) {
    //   push_all_uses(worklist, instr);
    //   // TODO: in theory could just put an unreach here
    //   instr->replace_all_uses(
    //       fir::ValueR{ctx->get_poisson_value(instr->get_type())});
    //   return;
    // }
    if (arg0_const->is_global()) {
      auto glob = arg0_const->as_global();
      if (glob->reloc_info.empty() && glob->is_constant &&
          (glob->linkage == fir::Linkage::Internal ||
           glob->linkage == fir::Linkage::LinkOnceODR)) {
        auto load_type = instr->get_type();
        auto out_bitwidth = load_type->get_bitwidth();
        if (load_type->is_int()) {
          if (out_bitwidth <= 64) {
            i128 val = 0;
            // TODO: kinda iffy with the loading oversize - prob not a problem
            // since the pages prob >8 byte aligned anyway :)
            if (out_bitwidth < 8) {
              val = *glob->init_value & ((1 << out_bitwidth) - 1);
            } else if (out_bitwidth == 8) {
              val = *glob->init_value;
            } else if (out_bitwidth < 16) {
              val = (*(u16 *)glob->init_value) & ((1 << out_bitwidth) - 1);
            } else if (out_bitwidth == 16) {
              val = (*(u16 *)glob->init_value);
            } else if (out_bitwidth < 32) {
              val = (*(u32 *)glob->init_value) & ((1 << out_bitwidth) - 1);
            } else if (out_bitwidth == 32) {
              val = (*(u32 *)glob->init_value);
            } else if (out_bitwidth < 64) {
              val = (*(u64 *)glob->init_value) & ((1 << out_bitwidth) - 1);
            } else if (out_bitwidth == 64) {
              val = (*(u64 *)glob->init_value);
            } else {
              UNREACH();
            }
            push_all_uses(worklist, instr);
            instr->replace_all_uses(
                fir::ValueR{ctx->get_constant_int(val, out_bitwidth)});
            instr.destroy();
            return true;
          }
        } else if (load_type->is_float()) {
          fir::ValueR new_val;
          if (out_bitwidth == 32) {
            f32 val = *(f32 *)glob->init_value;
            new_val = fir::ValueR{
                ctx->get_constant_value(val, ctx->get_float_type(32))};
          } else if (out_bitwidth == 64) {
            f64 val = *(f64 *)glob->init_value;
            new_val = fir::ValueR{
                ctx->get_constant_value(val, ctx->get_float_type(64))};
          } else {
            TODO("prob unsupported");
          }
          push_all_uses(worklist, instr);
          instr->replace_all_uses(new_val);
          instr.destroy();
          return true;
        } else if (load_type->is_vec()) {
          // TODO: implement
        } else if (load_type->is_ptr()) {
          i128 val = 0;
          val = (*(u64 *)glob->init_value);
          push_all_uses(worklist, instr);
          instr->replace_all_uses(
              fir::ValueR{ctx->get_constant_int(val, out_bitwidth)});
          instr.destroy();
          return true;
        } else {
          fmt::println("{:cd}", instr);
          TODO("implement global constant loading");
        }
      }
    }
  }
  // handle constant global + offset
  if (instr->args[0].is_instr()) {
    auto arg_instr = instr->args[0].as_instr();
    if (arg_instr->is(fir::InstrType::BinaryInstr) &&
        arg_instr->subtype == (u32)fir::BinaryInstrSubType::IntAdd &&
        arg_instr->args[0].is_constant() && arg_instr->args[1].is_constant()) {
      auto base_global = arg_instr->args[0].as_constant();
      auto offset = arg_instr->args[1].as_constant();
      if (base_global->is_global() && offset->is_int()) {
        auto offset_v = offset->as_int();
        auto global_v = base_global->as_global();
        for (auto reloc : global_v->reloc_info) {
          if (reloc.insert_offset == offset_v) {
            push_all_uses(worklist, instr);
            instr->replace_all_uses(fir::ValueR{reloc.ref});
            instr.destroy();
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool simplify_intrinsic(fir::Instr instr, fir::BasicBlock /*bb*/,
                        fir::Context &ctx, WorkList &worklist,
                        AttributerManager &man) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyIntrin");
  }
  auto sub_type = (fir::IntrinsicSubType)instr->subtype;
  if (sub_type == fir::IntrinsicSubType::Abs && instr->args[0].is_constant()) {
    push_all_uses(worklist, instr);
    auto c = instr->args[0].as_constant();
    if (c->is_int()) {
      auto val = std::abs(c->as_int());
      instr->replace_all_uses(
          fir::ValueR{ctx->get_constant_value(val, instr->get_type())});
      instr.destroy();
    }
    return true;
  }
  if (sub_type == fir::IntrinsicSubType::UMin ||
      sub_type == fir::IntrinsicSubType::UMax ||
      sub_type == fir::IntrinsicSubType::SMin ||
      sub_type == fir::IntrinsicSubType::SMax ||
      sub_type == fir::IntrinsicSubType::FMin ||
      sub_type == fir::IntrinsicSubType::FMax) {
    if (instr->args[0].is_constant() && instr->args[1].is_constant()) {
      push_all_uses(worklist, instr);
      switch (sub_type) {
        default:
          TODO("UNREACH");
        case fir::IntrinsicSubType::SMax: {
          push_all_uses(worklist, instr);
          auto val = std::max(instr->args[0].as_constant()->as_int(),
                              instr->args[1].as_constant()->as_int());
          instr->replace_all_uses(
              fir::ValueR{ctx->get_constant_value(val, instr->get_type())});
          instr.destroy();
          return true;
        }
        case fir::IntrinsicSubType::SMin: {
          push_all_uses(worklist, instr);
          auto val = std::min(instr->args[0].as_constant()->as_int(),
                              instr->args[1].as_constant()->as_int());
          instr->replace_all_uses(
              fir::ValueR{ctx->get_constant_value(val, instr->get_type())});
          instr.destroy();
          return true;
        }
        case fir::IntrinsicSubType::UMin: {
          push_all_uses(worklist, instr);
          auto val = std::min(
              std::bit_cast<u128>(instr->args[0].as_constant()->as_int()),
              std::bit_cast<u128>(instr->args[1].as_constant()->as_int()));
          instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(
              std::bit_cast<i128>(val), instr->get_type())});
          instr.destroy();
          return true;
        }
        case fir::IntrinsicSubType::UMax: {
          push_all_uses(worklist, instr);
          auto val = std::max(
              std::bit_cast<u128>(instr->args[0].as_constant()->as_int()),
              std::bit_cast<u128>(instr->args[1].as_constant()->as_int()));
          instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(
              std::bit_cast<i128>(val), instr->get_type())});
          instr.destroy();
          return true;
        }
        case fir::IntrinsicSubType::FMax: {
          push_all_uses(worklist, instr);
          auto val = std::max(instr->args[0].as_constant()->as_float(),
                              instr->args[1].as_constant()->as_float());
          instr->replace_all_uses(
              fir::ValueR{ctx->get_constant_value(val, instr->get_type())});
          instr.destroy();
          return true;
        }
        case fir::IntrinsicSubType::FMin: {
          push_all_uses(worklist, instr);
          auto val = std::min(instr->args[0].as_constant()->as_float(),
                              instr->args[1].as_constant()->as_float());
          instr->replace_all_uses(
              fir::ValueR{ctx->get_constant_value(val, instr->get_type())});
          instr.destroy();
          return true;
        }
      }
      return true;
    }
    if (instr->args[0].is_constant() && !instr->args[1].is_constant()) {
      auto old0 = instr->args[0];
      auto old1 = instr->args[1];
      instr.replace_arg(0, old1);
      instr.replace_arg(1, old0);
      return true;
    }
  }
  if (sub_type == fir::IntrinsicSubType::FAbs && instr->args[0].is_constant()) {
    auto ty = instr->get_type();
    if (ty->is_float()) {
      auto width = ty->as_float();

      push_all_uses(worklist, instr);
      if (width == 32) {
        auto val = std::fabs(instr->args[0].as_constant()->as_f32());
        instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(val, ty)});
      } else if (width == 64) {
        auto val = std::fabs(instr->args[0].as_constant()->as_f64());
        instr->replace_all_uses(fir::ValueR{ctx->get_constant_value(val, ty)});
      } else {
        TODO("IMPL");
      }
      instr.destroy();
      return true;
    }
  }

  switch (sub_type) {
    default:
      break;
    case fir::IntrinsicSubType::UMin:
    case fir::IntrinsicSubType::UMax:
    case fir::IntrinsicSubType::SMin:
    case fir::IntrinsicSubType::FMin:
    case fir::IntrinsicSubType::FMax:
    case fir::IntrinsicSubType::SMax: {
      // IDK if these are worth the effort
      break;
    }

    case fir::IntrinsicSubType::Abs: {
      auto *a0 = man.get_or_create_analysis<KnownBits>(instr->args[0]);
      man.run(ctx);
      auto r = a0->msb_info();
      if (r == KnownBits::KnownZero) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(instr->args[0]);
        instr.destroy();
        return true;
      }
      if (r == KnownBits::KnownOne) {
        push_all_uses(worklist, instr);
        fir::Builder b{instr};
        auto negated_val =
            b.build_unary_op(instr->args[0], fir::UnaryInstrSubType::IntNeg);
        instr->replace_all_uses(negated_val);
        instr.destroy();
        return true;
      }
      break;
    }
    case fir::IntrinsicSubType::FAbs:
      break;
  }
  return false;
}

fir::ValueR propagate_load_through_select(fir::Instr select) {
  ASSERT(select->is(fir::InstrType::SelectInstr));
  ASSERT(select->get_n_uses() == 1);
  ASSERT(select->uses[0].user->is(fir::InstrType::LoadInstr));
  auto load = select->uses[0];
  fir::Builder bu{select};
  auto n_type = load.user.get_type();
  auto a = bu.build_load(n_type, select->args[1], false, false);
  auto b = bu.build_load(n_type, select->args[2], false, false);
  auto r = bu.build_select(n_type, select->args[0], a, b);
  load.user->replace_all_uses(r);
  return r;
}

bool simplify_alloca(fir::Instr instr, fir::BasicBlock /*bb*/,
                     fir::Context &ctx, WorkList &worklist, AliasAnalyis &aa) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyAlloca");
  }
  {
    // check if only read or written
    //  then we can delete
    TSet<fir::Use> visited;
    TVec<fir::Use> alloca_worklist{instr->uses.begin(), instr->uses.end()};
    bool is_read = false;
    bool is_written = false;
    bool escapes = false;
    TVec<fir::Use> mem2reg_blockers{};
    while (!alloca_worklist.empty()) {
      if (escapes) {
        break;
      }
      auto curr = alloca_worklist.back();
      alloca_worklist.pop_back();
      switch (curr.type) {
        case fir::UseType::NormalArg:
          if (curr.user->is(fir::InstrType::LoadInstr) && curr.argId == 0) {
            is_read |= curr.user->get_n_uses() > 0;
          } else if (curr.user->is(fir::InstrType::StoreInstr) &&
                     curr.argId == 0) {
            is_written = true;
          } else if ((curr.user->is(fir::InstrType::VectorInstr)) ||
                     (curr.user->is(fir::InstrType::BinaryInstr)) ||
                     (curr.user->is(fir::InstrType::SelectInstr) &&
                      curr.argId != 0)) {
            if (curr.user->is(fir::InstrType::SelectInstr)) {
              // binary_instrs we generally can fix by running sroa
              mem2reg_blockers.push_back(curr);
            }
            alloca_worklist.insert(alloca_worklist.end(),
                                   curr.user->uses.begin(),
                                   curr.user->uses.end());
          } else if (curr.user->is(fir::ConversionSubType::PtrToInt) ||
                     curr.user->is(fir::ConversionSubType::IntToPtr)) {
            alloca_worklist.insert(alloca_worklist.end(),
                                   curr.user->uses.begin(),
                                   curr.user->uses.end());
          } else if ((curr.user->is(fir::InstrType::StoreInstr) &&
                      curr.argId == 1) ||
                     curr.user->is(fir::InstrType::CallInstr) ||
                     curr.user->is(fir::InstrType::Intrinsic) ||
                     curr.user->is(fir::InstrType::ReturnInstr) ||
                     curr.user->is(fir::InstrType::ICmp) ||
                     curr.user->is(fir::InstrType::InsertValue)) {
            mem2reg_blockers.push_back(curr);
            escapes = true;
          } else {
            fmt::println("{}", instr);
            fmt::println("{}", curr.user);
            TODO("IMPL");
          }
          break;
        case fir::UseType::BBArg: {
          mem2reg_blockers.push_back(curr);
          const auto &bb_uses =
              curr.user->bbs[curr.argId].bb->args[curr.bbArgId]->uses;
          for (auto bb_use : bb_uses) {
            if (visited.contains(bb_use)) {
              continue;
            }
            visited.insert(bb_use);
            alloca_worklist.push_back(bb_use);
          }
        } break;
        case fir::UseType::BB:
          fmt::println("{}", instr);
          TODO("IMPL");
          break;
      }
    }

    // if alloca is only read then all reads are poision
    // if alloca is only written then we can discard
    if (escapes) {
      return false;
    }
    if ((!is_written && is_read) || (is_written && !is_read)) {
      push_all_uses(worklist, instr);
      TVec<fir::Use> use_copy{instr->uses.begin(), instr->uses.end()};
      auto p_val = fir::ValueR{ctx->get_poisson_value(ctx->get_ptr_type())};
      // TODO: idk if replacing justany use with poision is always legal
      // had issue before where replacing some arithmentics ended up with
      // poision load later one with segfaultedcould be similar cases that
      // cause bigger issues bool any_indirect_use = false;
      for (auto u : use_copy) {
        if (u.user->is(fir::InstrType::LoadInstr)) {
          push_all_uses(worklist, u.user);
          u.user->replace_all_uses(p_val);
          u.user.destroy();
          continue;
        }
        if (u.user->is(fir::InstrType::StoreInstr) && u.argId == 0) {
          u.user.destroy();
          continue;
        }
        // any_indirect_use = true;
        u.replace_use(p_val);
      }
      // if (!any_indirect_use) {
      instr.destroy();
      // }
      return true;
    }

    // if the mem2reg blockers all can be transformed we should definetly do
    // it only makes sense if its static and not too big
    if (!mem2reg_blockers.empty() && instr->args[0].is_constant() &&
        instr->args[0].as_constant()->as_int() < 64) {
      // fmt::println("{}", *instr->get_parent()->get_parent().func);
      // fmt::println("{}", instr);
      bool transform = true;
      for (auto b : mem2reg_blockers) {
        // fmt::println("{}", b);
        if (b.user->is(fir::InstrType::SelectInstr)) {
          if (b.user->get_n_uses() == 1 &&
              b.user->uses[0].user->is(fir::InstrType::LoadInstr) &&
              ((b.argId == 1 && aa.is_known_local_stack(b.user->args[2])) ||
               (b.argId == 2 && aa.is_known_local_stack(b.user->args[1])))) {
            continue;
          }
        } else if (b.type == fir::UseType::BBArg) {
          transform = false;
          continue;
        } else {
          fmt::println("{} {}", b.user, b.user->get_parent());
          TODO("okak");
        }
        transform = false;
      }
      if (transform) {
        for (auto b : mem2reg_blockers) {
          if (b.user->is(fir::InstrType::SelectInstr)) {
            auto load = b.user->uses[0].user;
            if (load->Atomic || load->Volatile) {
              continue;
            }
            push_all_uses(worklist, load);
            propagate_load_through_select(b.user);
            // load.destroy();
            return true;
          }
        }
        // fmt::println("{}", *instr->get_parent()->get_parent().func);
        // TODO("okey");
      }
    }
  }
  return false;
}

bool simplify_ext_byte_vector(fir::Instr instr, fir::Context &ctx,
                              WorkList &worklist, u32 extend_to,
                              u32 n_out_elems, fir::TypeR out_type) {
  if (instr->is(fir::InstrType::StoreInstr)) {
    auto out_type = ctx->get_int_type(extend_to * 8);
    fir::Builder bb{instr};
    auto in_addr = instr->args[0];
    auto in_val = instr->args[1];
    auto casted_val = bb.build_conversion_op(
        in_val,
        n_out_elems == 1 ? out_type : ctx->get_vec_type(out_type, n_out_elems),
        fir::ConversionSubType::BitCast);
    auto res = bb.build_store(in_addr, casted_val, false, false);
    push_all_uses(worklist, instr);
    instr->replace_all_uses(res);
    instr.destroy();
    return true;
  }
  if (instr->is(fir::InstrType::VectorInstr) &&
      instr->subtype == (u32)fir::VectorISubType::Broadcast) {
    fir::Builder bb{instr};
    auto in_val = instr->args[0];
    auto orig_v = bb.build_zext(in_val, out_type);
    auto v = orig_v;
    for (u32 i = 1; i < extend_to; i++) {
      auto ext = bb.build_zext(in_val, out_type);
      worklist.push_back({ext.as_instr(), ext.as_instr()->get_parent()});
      auto add = bb.build_binary_op(
          ext, fir::ValueR{ctx->get_constant_value(i * 8, out_type)},
          fir::BinaryInstrSubType::Shl);
      v = bb.build_binary_op(v, add, fir::BinaryInstrSubType::Or);
    }
    if (n_out_elems != 1) {
      v = bb.build_vbroadcast(v, ctx->get_vec_type(out_type, n_out_elems));
    }
    auto res = bb.build_conversion_op(v, instr->get_type(),
                                      fir::ConversionSubType::BitCast);
    worklist.push_back({orig_v.as_instr(), orig_v.as_instr()->get_parent()});
    push_all_uses(worklist, instr);
    instr->replace_all_uses(res);
    instr.destroy();
    return true;
  }
  return false;
}
void simplify_vector(fir::Instr instr, fir::BasicBlock /*bb*/,
                     fir::Context &ctx, WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyVector");
  }
  if (!instr->get_type()->is_vec()) {
    return;
  }
  const auto &v_t = instr->get_type()->as_vec();
  if (v_t.type == fir::VectorType::SubType::Integer && v_t.bitwidth == 8) {
    auto extend_to = v_t.member_number;
    if ((extend_to % 8 == 0 && extend_to / 8 <= 2) ||
        (extend_to % 4 == 0 && extend_to / 4 <= 4)) {
      auto n_out_elems = extend_to / 4;
      if (extend_to == 8) {
        n_out_elems = extend_to / 8;
      }
      auto out_type = ctx->get_int_type(extend_to * 8);
      if (simplify_ext_byte_vector(instr, ctx, worklist, extend_to, n_out_elems,
                                   out_type)) {
        return;
      }
    }
  }
  if (instr->is(fir::InstrType::LoadInstr) &&
      instr->args[0].get_type()->is_vec() && instr->args[0].is_instr()) {
    auto argi = instr->args[0].as_instr();
    if (argi->is(fir::BinaryInstrSubType::IntAdd) &&
        argi->args[1].is_constant() && argi->args[0].is_instr() &&
        argi->args[0].as_instr()->is(fir::VectorISubType::Broadcast)) {
      auto broad_cast = argi->args[0].as_instr();
      auto arg_broad = broad_cast->args[0];
      auto arg_argc = argi->args[1].as_constant()->as_vec();
      auto expected_spacing = instr->get_type()->as_vec().bitwidth;
      bool spacing_matches = true;

      for (size_t i = 1; i < arg_argc.members.size(); i++) {
        auto v1 = arg_argc.members[i - 1];
        auto v2 = arg_argc.members[i];
        ASSERT(v1->is_int());
        ASSERT(v2->is_int());
        auto v1i = v1->as_int();
        auto v2i = v2->as_int();
        if (v2i <= v1i || (v2i - v1i) * 8 != expected_spacing) {
          spacing_matches = false;
        }
      }

      if (spacing_matches) {
        fir::Builder buh{instr};
        auto r = buh.build_load(instr->get_type(), arg_broad, instr->Atomic,
                                instr->Volatile);
        instr->replace_all_uses(r);
        instr.destroy();
        if (argi->get_n_uses() == 0) {
          argi.destroy();
        }
        if (broad_cast->get_n_uses() == 0) {
          broad_cast.destroy();
        }
        return;
      }
    }
  }
  if (instr->subtype == (u32)fir::VectorISubType::Concat &&
      instr->args[0].is_instr() && instr->args[1].is_instr()) {
    auto a1 = instr->args[0].as_instr();
    auto a2 = instr->args[1].as_instr();
    if (a1->is(fir::VectorISubType::ExtractLow) &&
        a2->is(fir::VectorISubType::ExtractHigh) &&
        a1->args[0] == a2->args[0]) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(a1->args[0]);
      instr.destroy();
      return;
    }
  }
  if (instr->subtype == (u32)fir::VectorISubType::ExtractHigh &&
      instr->args[0].is_instr()) {
    auto a1 = instr->args[0].as_instr();
    if (a1->is(fir::VectorISubType::Concat)) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(a1->args[0]);
      instr.destroy();
      if (a1->get_n_uses() == 0) {
        a1.destroy();
      }
      return;
    }
  } else if (instr->subtype == (u32)fir::VectorISubType::ExtractLow &&
             instr->args[0].is_instr()) {
    auto a1 = instr->args[0].as_instr();
    if (a1->is(fir::VectorISubType::Concat)) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(a1->args[1]);
      instr.destroy();
      if (a1->get_n_uses() == 0) {
        a1.destroy();
      }
      return;
    }
  }
}

bool simplify_extract(fir::Instr instr, WorkList &worklist) {
  if (TRACY_DEBUG_INST_SIMPLIFY) {
    ZoneScopedN("SimplifyExtract");
  }
  if (instr->args[0].is_instr()) {
    auto argi = instr->args[0].as_instr();
    if (argi->is(fir::VectorISubType::Broadcast)) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(argi->args[0]);
      instr.destroy();
      return true;
    }
    if (argi->is(fir::InstrType::InsertValue)) {
      if (instr->args[1].eql(argi->args[2])) {
        push_all_uses(worklist, instr);
        instr->replace_all_uses(argi->args[1]);
        instr.destroy();
        return true;
      }
      fir::Builder bb{instr};
      fir::ValueR v[1] = {instr->args[1]};
      auto res = bb.build_extract_value(argi->args[0], v, instr->get_type());
      push_all_uses(worklist, instr);
      instr->replace_all_uses(res);
      instr.destroy();
      return true;
    }
  }
  return false;
}

bool simplify(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
              WorkList &worklist, AttributerManager &man, AliasAnalyis &anal) {
  ZoneScopedN("SimplifyInstr");
  using namespace foptim::fir;
  (void)anal;
  auto instr_ty = instr->get_instr_type();
  if (instr_ty == InstrType::BinaryInstr) {
    return simplify_binary(instr, bb, ctx, worklist, man);
  }
  if (instr_ty == InstrType::UnaryInstr) {
    return simplify_unary(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::ICmp) {
    return simplify_icmp(instr, bb, ctx, worklist, man);
  }
  if (instr_ty == InstrType::StoreInstr) {
    if (simplify_store(instr)) {
      return true;
    }
  }
  if (instr_ty == InstrType::LoadInstr &&
      simplify_load(instr, bb, ctx, worklist)) {
    return true;
  }
  if (instr_ty == InstrType::VectorInstr ||
      (instr_ty == InstrType::LoadInstr && instr->get_type()->is_vec()) ||
      (instr_ty == InstrType::StoreInstr && instr->get_type()->is_vec())) {
    simplify_vector(instr, bb, ctx, worklist);
    return true;
  }
  if (instr_ty == InstrType::FCmp) {
    return simplify_fcmp(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::SelectInstr) {
    return simplify_select(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::CondBranchInstr) {
    return simplify_cond_branch(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::SwitchInstr) {
    simplify_switch_branch(instr, bb, ctx, worklist);
    return true;
  }
  if (instr_ty == InstrType::SExt || instr_ty == InstrType::ZExt) {
    return simplify_extend(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::ITrunc) {
    return simplify_itrunc(instr, bb, ctx, worklist, man);
  }
  if (instr_ty == InstrType::Conversion) {
    return simplify_conversion(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::CallInstr) {
    return simplify_call(instr, bb, ctx, worklist);
  }
  if (instr_ty == InstrType::Intrinsic) {
    return simplify_intrinsic(instr, bb, ctx, worklist, man);
  }
  if (instr_ty == InstrType::ExtractValue) {
    return simplify_extract(instr, worklist);
  }
  if (instr_ty == InstrType::AllocaInstr) {
    return simplify_alloca(instr, bb, ctx, worklist, anal);
  }
  return false;
}
}  // namespace
}  // namespace InstSimp

void InstSimplify::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedNC("InstSimplify", COLOR_OPTIMF);
  using namespace foptim::fir;
  AttributerManager man;
  AliasAnalyis anal{};

  // TODO: maybe replace with actual queue
  TVec<WorkItem> worklist;
  for (BasicBlock bb : func.basic_blocks) {
    auto &instrs = bb->get_instrs();
    for (auto &instr : instrs) {
      worklist.emplace_back(instr, bb);
    }
  }

  while (!worklist.empty()) {
    auto [instr, bb] = worklist.back();
    worklist.pop_back();
    if (!instr.is_valid() || !instr->parent.is_valid()) {
      continue;
    }
    if (InstSimp::simplify(instr, bb, ctx, worklist, man, anal)) {
      man.reset();
    }
  }
}

}  // namespace foptim::optim
