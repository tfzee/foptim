#include "sccp.hpp"

#include <bit>
#include <cmath>
#include <fmt/base.h>

#include <algorithm>
#include <type_traits>

#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "utils/helpers.hpp"
#include "utils/todo.hpp"

namespace foptim::optim {

std::optional<fir::ConstantValueR>
SCCP::ConstantValue::toConstantValue(fir::Context ctx, fir::TypeR t) {
  switch (type) {
  case ValueType::Top:
  case ValueType::Bottom:
    return {};
  case ValueType::Poison:
    return ctx->get_poisson_value(t);
  case ValueType::NullPtr:
    ASSERT(vals.size() == 1);
    return ctx->get_constant_null();
  case ValueType::Float:
    if (vals.size() == 1) {
      return ctx->get_constant_value(vals[0].f, t);
    } else {
      IRVec<fir::ConstantValueR> args;
      auto elem_width = t->as_vec().bitwidth;
      for (auto &v : vals) {
        if (elem_width == 32) {
          args.push_back(ctx->get_constant_value(
              std::bit_cast<f32>(static_cast<u32>(std::bit_cast<u64>(v.f))),
              ctx->get_float_type(elem_width)));
        } else {
          args.push_back(
              ctx->get_constant_value(v.f, ctx->get_float_type(elem_width)));
        }
      }
      return ctx->get_constant_value(args, t);
    }
  case ValueType::Int:
    if (vals.size() == 1) {
      return ctx->intern_constant(
          fir::ConstantValue(vals[0].i.sext_or_trunc(t->get_bitwidth()), t));
    } else {
      IRVec<fir::ConstantValueR> args;
      auto elem_width = t->as_vec().bitwidth;
      for (auto &v : vals) {
        args.push_back(ctx->get_constant_int(v.i.sext_or_trunc(elem_width)));
      }
      return ctx->get_constant_value(args, t);
    }
  case ValueType::Fptr:
    ASSERT(vals.size() == 1);
    return ctx->get_constant_value(vals[0].fptr);
  case ValueType::Gptr:
    ASSERT(vals.size() == 1);
    return ctx->get_constant_value(vals[0].gptr);
  }
  TODO("unreach?");
}

std::optional<SCCP::ConstantValue>
SCCP::ConstantValue::loadConstant(u8 *v, fir::TypeR c, fir::Context &ctx)

{
  auto bitwidth = c->get_bitwidth();
  if (c->is_float() && bitwidth == 32) {
    return ConstantValue{
        .type = ValueType::Float,
        .vals = {{.f = std::bit_cast<f64>(static_cast<u64>(
                      std::bit_cast<u32>(*reinterpret_cast<f32 *>(v))))}},
        .vtype = c};
  }
  if (c->is_float() && bitwidth == 64) {
    return ConstantValue{.type = ValueType::Float,
                         .vals = {{.f = *reinterpret_cast<f64 *>(v)}},
                         .vtype = c};
  }
  if ((c->is_int() && bitwidth == 64) || c->is_ptr()) {
    auto val = (*reinterpret_cast<u64 *>(v));
    if (c->is_ptr() && val == 0) {
      return ConstantValue{
          .type = ValueType::NullPtr, .vals = {{.i = APInt::zero(64)}}, .vtype = c};
    }
    return ConstantValue{
        .type = ValueType::Int,
        .vals = {{.i = APInt(64, val)}},
        .vtype = c};
  }
  if (c->is_int() && bitwidth == 32) {
    return ConstantValue{.type = ValueType::Int,
                         .vals = {{.i = APInt(32, *reinterpret_cast<u32 *>(v))}},
                         .vtype = c};
  }
  if (c->is_int() && bitwidth == 16) {
    return ConstantValue{.type = ValueType::Int,
                         .vals = {{.i = APInt(16, *reinterpret_cast<u16 *>(v))}},
                         .vtype = c};
  }
  if (c->is_int() && bitwidth == 8) {
    return ConstantValue{
        .type = ValueType::Int,
        .vals = {{.i = APInt(8, *v)}},
        .vtype = c};
  }
  if (c->is_vec()) {
    const auto &vec = c->as_vec();
    auto n_lanes = vec.member_number;
    auto res =
        ConstantValue{.type = vec.type == fir::VectorType::SubType::Integer
                                  ? ValueType::Int
                                  : ValueType::Float,
                      .vals = {},
                      .vtype = c};
    for (size_t l = 0; l < n_lanes; l++) {
      std::optional<ConstantValue> el;
      if (vec.type == fir::VectorType::SubType::Integer) {
        el = loadConstant(v + (l * (vec.bitwidth / 8)),
                          ctx->get_int_type(vec.bitwidth), ctx);
      } else if (vec.type == fir::VectorType::SubType::Floating) {
        el = loadConstant(v + (l * (vec.bitwidth / 8)),
                          ctx->get_float_type(vec.bitwidth), ctx);
      } else {
        fmt::println("Data load1 {:cd}", c);
        TODO("impl");
      }
      if (!el) {
        return {};
      }
      ASSERT(el.value().vals.size() == 1);
      res.vals.push_back(el.value().vals[0]);
      // fmt::println(">subf {}", res.vals.back().f);
    }
    return res;
  }
  fmt::println("Data load {:cd}", c);
  TODO("impl");
}

bool SCCP::ConstantValue::operator==(const ConstantValue &other) const {
  if (type != other.type) {
    return false;
  }
  if (is_const()) {
    if (vals.size() != other.vals.size()) {
      return false;
    }
    for (size_t i = 0; i < vals.size(); i++) {
      switch (type) {
      case ValueType::Top:
      case ValueType::Bottom:
      case ValueType::NullPtr:
      case ValueType::Poison:
        continue;
      case ValueType::Float:
        if (vals[i].f != other.vals[i].f) {
          return false;
        }
        continue;
      case ValueType::Int:
        if (vals[i].i != other.vals[i].i) {
          return false;
        }
        continue;
      case ValueType::Gptr:
        if (vals[i].gptr != other.vals[i].gptr) {
          return false;
        }
        continue;
      case ValueType::Fptr:
        if (vals[i].fptr != other.vals[i].fptr) {
          return false;
        }
        continue;
      }
    }
  }
  return true;
}

namespace {
// amounts that are out of range for any type just need to stay out of range
u32 shift_amount(const APInt &b) {
  return b.fits_u64() ? static_cast<u32>(std::min<u64>(b.to_u64(), 1 << 20))
                      : (1 << 20);
}

template <class T>
T const_eval_bin(fir::Instr instr, fir::TypeR out_type, T a, T b) {
  (void)out_type;
  switch (static_cast<fir::BinaryInstrSubType>(instr->get_instr_subtype())) {
  default:
    fmt::println("{}", instr);
    TODO("implement instr");
    UNREACH();
  case fir::BinaryInstrSubType::INVALID:
    UNREACH();
  case fir::BinaryInstrSubType::And:
    if constexpr (std::is_same_v<T, f32>) {
      return std::bit_cast<f32>(std::bit_cast<u32>(a) & std::bit_cast<u32>(b));
    } else if constexpr (std::is_same_v<T, f64>) {
      return std::bit_cast<f64>(std::bit_cast<u64>(a) & std::bit_cast<u64>(b));
    } else {
      return a & b;
    }
  case fir::BinaryInstrSubType::Xor:
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return a ^ b;
    }
  case fir::BinaryInstrSubType::Or:
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return a | b;
    }
  case fir::BinaryInstrSubType::Shl:
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return a.shl(shift_amount(b));
    }
  case fir::BinaryInstrSubType::Shr:
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return a.lshr(shift_amount(b));
    }
  case fir::BinaryInstrSubType::AShr: {
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return a.ashr(shift_amount(b));
    }
  }
  case fir::BinaryInstrSubType::IntSub:
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return a - b;
    }
  case fir::BinaryInstrSubType::PtrAdd:
  case fir::BinaryInstrSubType::IntAdd:
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      // TODO: inbounds with null add is poision
      //  if (instr->InBounds && a == 0) {
      //    return ctx->get_poision_value(out_type);
      //  }
      return a + b;
    }
  case fir::BinaryInstrSubType::IntUDiv: {
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      // division by zero is undefined, any value will do
      return b.is_zero() ? b : a.udiv(b);
    }
  }
  case fir::BinaryInstrSubType::IntSDiv: {
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return b.is_zero() ? b : a.sdiv(b);
    }
  }
  case fir::BinaryInstrSubType::IntSRem: {
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return b.is_zero() ? b : a.srem(b);
    }
  }
  case fir::BinaryInstrSubType::IntURem: {
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return b.is_zero() ? b : a.urem(b);
    }
  }
  case fir::BinaryInstrSubType::IntMul:
    if constexpr (std::is_floating_point_v<T>) {
      UNREACH();
    } else {
      return a * b;
    }
  case fir::BinaryInstrSubType::FloatAdd:
    if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, f64>) {
      return a + b;
    } else {
      TODO("support other bitwidths");
    }
  case fir::BinaryInstrSubType::FloatMul:
    if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, f64>) {
      return a * b;
    } else {
      TODO("support other bitwidths");
    }
  case fir::BinaryInstrSubType::FloatDiv:
    if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, f64>) {
      return a / b;
    } else {
      TODO("support other bitwidths");
    }
  case fir::BinaryInstrSubType::FloatSub:
    if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, f64>) {
      return a - b;
    } else {
      TODO("support other bitwidths");
    }
  }
}
} // namespace

SCCP::ConstantValue SCCP::eval_binary_instr(fir::Context &ctx,
                                            fir::Instr instr) {
  (void)ctx;
  auto a = eval(instr->get_arg(0));
  auto b = eval(instr->get_arg(1));

  if (a.is_bottom() || b.is_bottom()) {
    return ConstantValue::Bottom();
  }
  if (!a.is_const() || !b.is_const()) {
    return ConstantValue::Top();
  }

  if ((!a.is_int() && !a.is_float() && !a.is_null()) ||
      (!b.is_int() && !b.is_float() && !b.is_null())) {
    failure(
        {.reason = "Cannot do SCCP on binary expr using non integers/floats",
         .loc = instr});
    return ConstantValue::Top();
  }

  auto out_type = a.get_type();

  TVec<ConstantValue::Value> v_outs;
  if (a.is_int() || a.is_null()) {
    for (size_t i = 0; i < a.vals.size(); i++) {
      APInt ai = a.as_apint(i);
      APInt bi = b.as_apint(i);
      if (bi.bits() != ai.bits()) {
        bi = bi.sext_or_trunc(ai.bits());
      }
      auto c = ConstantValue::Value{.i = const_eval_bin(instr, out_type, ai, bi)};
      v_outs.push_back(c);
    }
  } else if (a.is_float()) {
    for (size_t i = 0; i < a.vals.size(); i++) {
      auto is_32bit = a.vtype->get_bitwidth() == 32 ||
                      (a.vtype->is_vec() && a.vtype->as_vec().bitwidth == 32);
      auto is_64bit = a.vtype->get_bitwidth() == 64 ||
                      (a.vtype->is_vec() && a.vtype->as_vec().bitwidth == 64);
      if (is_32bit) {
        auto v = const_eval_bin(instr, out_type, a.as_f32(i), b.as_f32(i));
        auto c = ConstantValue::Value{
            .f = std::bit_cast<f64>(static_cast<u64>(std::bit_cast<u32>(v)))};
        v_outs.push_back(c);
      } else if (is_64bit) {
        auto v = const_eval_bin(instr, out_type, a.as_f64(i), b.as_f64(i));
        auto c = ConstantValue::Value{.f = v};
        v_outs.push_back(c);
      } else {
        fmt::println("{}", instr);
        TODO("impl");
      }
    }
  } else {
    fmt::println("{}", instr);
    TODO("impl");
  }
  return SCCP::ConstantValue{
      .type = a.type, .vals = std::move(v_outs), .vtype = out_type};
}

PreservedAnalysis SCCP::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("SCCP");
  cfg.update(func, false);
  cfg_worklist.push_back(func.get_entry());
  for (auto bb : func.get_bbs()) {
    for (size_t arg_id = 0; arg_id < bb->get_args().size(); arg_id++) {
      values.insert({fir::ValueR(bb->args[arg_id]), ConstantValue::Top()});
    }
    for (auto instr : bb->instructions) {
      if (instr->is_multi_result()) {
        for (u8 i = 0; i < instr->n_results(); i++) {
          values.insert({instr.result(i), ConstantValue::Top()});
        }
      } else {
        values.insert({instr.result(), ConstantValue::Top()});
      }
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
        eval_and_update(ctx, bb->instructions[instr_id]);
      }

      // TODO("handle terminator phi stuff merging somehow??\n");
    }
    while (!ssa_worklist.empty()) {
      fir::Use &use = ssa_worklist.front();
      ssa_worklist.pop_front();
      if (!use.user.is_valid()) {
        continue;
      }
      eval_and_update(ctx, use.user);
    }
  }
  // dump();
  modified |= execute(ctx);
  if (!modified) {
    return PreservedAnalysis::all();
  }
  // folded branches change edges, everything else only replaces uses
  return cfg_modified ? PreservedAnalysis::none()
                      : PreservedAnalysis::cfg_only();
}

void SCCP::eval_meets(fir::BasicBlock bb, size_t bb_id) {
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
        if (a == b) {
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

void SCCP::dump() {
  fmt::println("DUMP SCCP: ");
  for (auto &[val, consta] : values) {
    fmt::println("{}: ",
                 reinterpret_cast<const void *>(val.instr.get_raw_ptr()));
    ASSERT(val.is_valid(false));
    fmt::println("{}: ", val);
    switch (consta.type) {
    case ConstantValue::ValueType::Top:
      fmt::println("TOP\n");
      break;
    case ConstantValue::ValueType::Bottom:
      fmt::println("BOT\n");
      break;
    case ConstantValue::ValueType::Float:
    case ConstantValue::ValueType::Int:
    case ConstantValue::ValueType::Gptr:
    case ConstantValue::ValueType::Fptr:
    case ConstantValue::ValueType::NullPtr:
    case ConstantValue::ValueType::Poison:
      ASSERT(consta.get_type().is_valid());
      fmt::println("VAL {}\n", consta.get_type().is_valid());
      break;
    }
  }
}

void SCCP::eval_and_update(fir::Context &ctx, fir::Instr instr) {
  // only calls have several results and their results are never constant
  if (instr->is_multi_result()) {
    return;
  }
  fir::ValueR value = instr.result();

  ConstantValue new_value = ConstantValue::Top();

  if (value.is_constant()) {
    TODO("constant\n");
  } else if (value.is_instr()) {
    new_value = eval_instr(ctx, value.as_instr());
    if (value.is_valid(true) && values.at(value) != new_value) {
      values.at(value) = new_value;
      for (auto &use : *value.get_uses()) {
        if (reachable_bb.contains(use.user->get_parent())) {
          ssa_worklist.push_back(use);
        }
      }
    }

    if (new_value.is_const() && value.get_n_uses() > 0) {
      auto res_co = new_value.toConstantValue(ctx, value.get_type());
      if (res_co) {
        value.replace_all_uses(fir::ValueR{res_co.value()});
        modified = true;
      }
    }
  } else if (value.is_bb_arg()) {
    UNREACH();
  }
}
SCCP::ConstantValue SCCP::eval_instr(fir::Context &ctx, fir::Instr instr) {
  switch (instr->get_instr_type()) {
  case fir::InstrType::Fence: {
    return ConstantValue::Bottom();
  }
  case fir::InstrType::AtomicRMW: {
    return ConstantValue::Top();
  }
  case fir::InstrType::VectorInstr: {
    switch (static_cast<fir::VectorISubType>(instr->get_instr_subtype())) {
    case fir::VectorISubType::HorizontalAdd: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() && (!a.is_int() && !a.is_float())) {
        return ConstantValue::Top();
      }

      if (a.is_int()) {
        APInt sum = APInt::zero(instr->result_type()->get_bitwidth());
        for (auto &m : a.vals) {
          sum += m.i.zext_or_trunc(sum.bits());
        }
        return ConstantValue::Constant(std::move(sum), instr->result_type());
      }
      if (a.is_float() && ((a.vtype->is_float() && a.vtype->as_float() == 64) ||
                           (a.is_float() && a.vtype->is_vec() &&
                            a.vtype->as_vec().bitwidth == 64))) {
        f64 res = 0;
        for (auto &m : a.vals) {
          res += m.f;
        }
        return ConstantValue::Constant(res, instr->result_type());
      }
      if (a.is_float() && ((a.vtype->is_float() && a.vtype->as_float() == 32) ||
                           (a.is_float() && a.vtype->is_vec() &&
                            a.vtype->as_vec().bitwidth == 32))) {
        f32 res = 0;
        for (auto &m : a.vals) {
          res += std::bit_cast<f32>(static_cast<u32>(std::bit_cast<u64>(m.f)));
        }
        return ConstantValue::Constant(res, instr->result_type());
      }
      return ConstantValue::Top();
    }
    case fir::VectorISubType::Broadcast: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() && (!a.is_int() && !a.is_float())) {
        return ConstantValue::Top();
      }
      const auto &vtype = instr->result_type()->as_vec();
      if (a.is_int()) {
        TVec<ConstantValue::Value> v_outs = {};
        for (size_t i = 0; i < vtype.member_number; i++) {
          v_outs.push_back({.i = a.as_apint()});
        }
        return {.type = ConstantValue::ValueType::Int,
                .vals = v_outs,
                .vtype = instr->result_type()};
      }
      if (a.is_float()) {
        TVec<ConstantValue::Value> v_outs = {};
        for (size_t i = 0; i < vtype.member_number; i++) {
          if (a.vtype->is_float() && a.vtype->as_float() == 64) {
            v_outs.push_back({.f = a.as_f64()});
          } else if (a.vtype->is_float() && a.vtype->as_float() == 32) {
            v_outs.push_back({.f = std::bit_cast<f64>(static_cast<u64>(
                                  std::bit_cast<u32>(a.as_f32())))});
          }
        }
        return {.type = ConstantValue::ValueType::Float,
                .vals = v_outs,
                .vtype = instr->result_type()};
      }
      return ConstantValue::Top();
    }
    case fir::VectorISubType::INVALID:
    case fir::VectorISubType::Shuffle:
    case fir::VectorISubType::Concat:
    case fir::VectorISubType::ExtractHigh:
    case fir::VectorISubType::ExtractLow:
    case fir::VectorISubType::HorizontalMul:
      break;
    }
    return ConstantValue::Top();
  }
  case fir::InstrType::UnaryInstr: {
    auto a = eval(instr->get_arg(0));
    ASSERT(a.vals.size() <= 1);

    if (a.is_bottom()) {
      return ConstantValue::Bottom();
    }
    if (!a.is_const()) {
      return ConstantValue::Top();
    }

    if ((!a.is_int() && !a.is_float())) {
      failure(
          {.reason = "Cannot do SCCP on binary expr using non integers/floats",
           .loc = instr});
      return ConstantValue::Bottom();
    }

    auto out_type = a.get_type();

    switch (static_cast<fir::UnaryInstrSubType>(instr->get_instr_subtype())) {
    case fir::UnaryInstrSubType::INVALID:
      UNREACH();
    case fir::UnaryInstrSubType::FloatNeg: {
      auto width = out_type->as_float();
      if (width == 32) {
        return ConstantValue::Constant(
            ctx->get_constant_value(-a.as_f32(), out_type));
      }
      if (width == 64) {
        return ConstantValue::Constant(
            ctx->get_constant_value(-a.as_f64(), out_type));
      }
      TODO("UNREACH?");
    }
    case fir::UnaryInstrSubType::IntNeg:
      return ConstantValue::Constant(
          ctx->get_constant_int(-a.as_apint()));
    case fir::UnaryInstrSubType::Not: {
      // return ConstantValue::Bottom();
      return ConstantValue::Constant(ctx->get_constant_int(~a.as_apint()));
    }
    case fir::UnaryInstrSubType::FloatSqrt: {
      auto width = out_type->as_float();
      if (width == 32) {
        return ConstantValue::Constant(
            ctx->get_constant_value(std::sqrt(a.as_f32()), out_type));
      }
      if (width == 64) {
        return ConstantValue::Constant(
            ctx->get_constant_value(std::sqrt(a.as_f64()), out_type));
      }
      TODO("UNREACH?");
    }

    default:
      fmt::println("{}", instr);
      TODO("implement instr");
      UNREACH();
    }
  }
  case fir::InstrType::Intrinsic: {
    bool is_signed = false;
    // TODO: implement constant propagation
    switch (static_cast<fir::IntrinsicSubType>(instr->get_instr_subtype())) {
    case fir::IntrinsicSubType::FCeil:
    case fir::IntrinsicSubType::FFloor:
    case fir::IntrinsicSubType::FTrunc:
    case fir::IntrinsicSubType::FRound: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() && (!a.is_float())) {
        return ConstantValue::Top();
      }
      fmt::println("{:cd}", instr);
      TODO("Impl");
    }
    case fir::IntrinsicSubType::FAbs: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() && (!a.is_float())) {
        return ConstantValue::Top();
      }
      for (auto &m : a.vals) {
        if (((a.vtype->is_float() && a.vtype->as_float() == 64) ||
             (a.vtype->is_vec() && a.vtype->as_vec().bitwidth == 64))) {
          m.f = std::abs(m.f);
        } else if (((a.vtype->is_float() && a.vtype->as_float() == 32) ||
                    (a.vtype->is_vec() && a.vtype->as_vec().bitwidth == 32))) {
          m.f = std::bit_cast<f64>(static_cast<u64>(std::bit_cast<u32>(std::abs(
              std::bit_cast<f32>(static_cast<u32>(std::bit_cast<u64>(m.f)))))));
        }
      }
      return a;
    }
    case fir::IntrinsicSubType::Abs: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() && (!a.is_float())) {
        return ConstantValue::Top();
      }
      for (auto &m : a.vals) {
        if (m.i.is_neg()) {
          m.i = -m.i;
        }
      }
      return a;
    }
    case fir::IntrinsicSubType::IsConstant: {
      auto a = eval(instr->get_arg(0));
      a.vtype = ctx->get_int_type(1);
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const()) {
        return ConstantValue::Top();
      }
      for (auto &m : a.vals) {
        m.i = APInt(1, u64{1});
      }
      return a;
    }
    case fir::IntrinsicSubType::SMax:
      is_signed = true;
    case fir::IntrinsicSubType::UMax: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if ((!a.is_const() || !a.is_int()) || (!b.is_const() || !b.is_int())) {
        return ConstantValue::Top();
      }
      for (size_t i = 0; i < a.vals.size(); i++) {
        const bool b_greater = is_signed ? a.vals[i].i.slt(b.vals[i].i)
                                         : a.vals[i].i.ult(b.vals[i].i);
        if (b_greater) {
          a.vals[i].i = b.vals[i].i;
        }
      }
      return a;
    }
    case fir::IntrinsicSubType::SMin:
      is_signed = true;
    case fir::IntrinsicSubType::UMin: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if ((!a.is_const() || !a.is_int()) || (!b.is_const() || !b.is_int())) {
        return ConstantValue::Top();
      }
      for (size_t i = 0; i < a.vals.size(); i++) {
        const bool b_less = is_signed ? b.vals[i].i.slt(a.vals[i].i)
                                      : b.vals[i].i.ult(a.vals[i].i);
        if (b_less) {
          a.vals[i].i = b.vals[i].i;
        }
      }
      return a;
    }
    case fir::IntrinsicSubType::FMinimum: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() || !b.is_const()) {
        return ConstantValue::Top();
      }
      for (size_t i = 0; i < a.vals.size(); i++) {
        if (std::isnan(a.vals[i].f)) {
          a.vals[i].f = a.vals[i].f;
        }
        a.vals[i].f = std::min(a.vals[i].f, b.vals[i].f);
      }
      return a;
    }
    case fir::IntrinsicSubType::FMaximum: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() || !b.is_const()) {
        return ConstantValue::Top();
      }
      for (size_t i = 0; i < a.vals.size(); i++) {
        if (std::isnan(a.vals[i].f)) {
          a.vals[i].f = a.vals[i].f;
        }
        a.vals[i].f = std::max(a.vals[i].f, b.vals[i].f);
      }
      return a;
    }
    case fir::IntrinsicSubType::FMin: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() || !b.is_const()) {
        return ConstantValue::Top();
      }
      for (size_t i = 0; i < a.vals.size(); i++) {
        a.vals[i].f = std::min(a.vals[i].f, b.vals[i].f);
      }
      return a;
    }
    case fir::IntrinsicSubType::FMax: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const() || !b.is_const()) {
        return ConstantValue::Top();
      }
      for (size_t i = 0; i < a.vals.size(); i++) {
        a.vals[i].f = std::max(a.vals[i].f, b.vals[i].f);
      }
      return a;
    }
    case fir::IntrinsicSubType::CTTZ: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const()) {
        return ConstantValue::Top();
      }
      ASSERT(a.is_int());
      // if b is not known we dont know how to handle 0
      if (!b.is_const()) {
        for (size_t i = 0; i < a.vals.size(); i++) {
          if (a.vals[0].i.is_zero()) {
            return ConstantValue::Top();
          }
        }
      }
      const auto arg_ty = instr->get_arg(0).get_type();
      const u32 width =
          arg_ty->is_vec() ? arg_ty->as_vec().bitwidth : arg_ty->get_bitwidth();
      for (auto &val : a.vals) {
        if (val.i.is_zero()) {
          // zero input is poison if b is set, otherwise the width
          if (!b.vals[0].i.is_zero()) {
            return ConstantValue::Top();
          }
          val.i = APInt(width, u64{width});
        } else {
          val.i = APInt(width, u64{val.i.ctz()});
        }
      }
      return a;
    }
    case fir::IntrinsicSubType::CTLZ: {
      auto a = eval(instr->get_arg(0));
      auto b = eval(instr->get_arg(1));
      if (a.is_bottom() || b.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const()) {
        return ConstantValue::Top();
      }
      ASSERT(a.is_int());
      // if b is not known we dont know how to handle 0
      if (!b.is_const()) {
        for (size_t i = 0; i < a.vals.size(); i++) {
          if (a.vals[0].i.is_zero()) {
            return ConstantValue::Top();
          }
        }
      }
      const auto arg_ty = instr->get_arg(0).get_type();
      const u32 width =
          arg_ty->is_vec() ? arg_ty->as_vec().bitwidth : arg_ty->get_bitwidth();
      for (auto &val : a.vals) {
        if (val.i.is_zero()) {
          // zero input is poison if b is set, otherwise the width
          if (!b.vals[0].i.is_zero()) {
            return ConstantValue::Top();
          }
          val.i = APInt(width, u64{width});
        } else {
          val.i = APInt(width, u64{val.i.clz()});
        }
      }
      return a;
    }
    case fir::IntrinsicSubType::PopCnt: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const()) {
        return ConstantValue::Top();
      }
      TODO("popcnt impl sccp");
    }
    case fir::IntrinsicSubType::Bswap: {
      auto a = eval(instr->get_arg(0));
      if (a.is_bottom()) {
        return ConstantValue::Bottom();
      }
      if (!a.is_const()) {
        return ConstantValue::Top();
      }
      const auto arg_ty = instr->get_arg(0).get_type();
      const u32 width =
          arg_ty->is_vec() ? arg_ty->as_vec().bitwidth : arg_ty->get_bitwidth();
      for (auto &val : a.vals) {
        APInt res = APInt::zero(width);
        for (u32 byte = 0; byte < width / 8; byte++) {
          const APInt b8 = val.i.lshr(8 * byte).trunc(8).zext(width);
          res |= b8.shl(width - 8 - 8 * byte);
        }
        val.i = std::move(res);
      }
      return a;
    }
    case fir::IntrinsicSubType::INVALID:
    case fir::IntrinsicSubType::VA_start:
    case fir::IntrinsicSubType::VA_end:
    case fir::IntrinsicSubType::Memcmp:
      return ConstantValue::Top();
    case fir::IntrinsicSubType::Memset:
    case fir::IntrinsicSubType::Memcpy:
    case fir::IntrinsicSubType::Memmove:
      break;
    }
    return ConstantValue::Bottom();
  }
  case fir::InstrType::BinaryInstr:
    return eval_binary_instr(ctx, instr);
  case fir::InstrType::BranchInstr: {
    const auto &target = instr->get_bb_args();
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
  case fir::InstrType::SwitchInstr: {
    const auto &targets = instr->get_bb_args();
    const auto func = instr->get_parent()->get_parent();
    // TODO: IMPROVE IT
    for (const auto &arg : targets) {
      if (!bottom_bbs.contains(arg.bb)) {
        cfg_worklist.push_back(arg.bb);
        bottom_bbs.insert(arg.bb);
      }
      {
        const size_t bb_id = func->bb_id(arg.bb);
        eval_meets(arg.bb, bb_id);
      }
    }
    return ConstantValue::Top();
  }
  case fir::InstrType::CondBranchInstr: {
    const auto &targets = instr->get_bb_args();
    ASSERT(targets.size() == 2);
    auto arg = eval(instr->get_arg(0));
    ASSERT(arg.vals.size() <= 1);
    const auto func = instr->get_parent()->get_parent();
    // ASSERT(!arg.is_bottom());
    if (arg.is_bottom()) {
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

    bool cond = false;
    if (arg.is_poison()) {
    } else {
      ASSERT(arg.is_int());
      cond = !arg.as_apint().is_zero();
    }

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

    modified = true;
    cfg_modified = true;
    values.erase(instr.result());
    instr.clear_bbs();
    instr.clear_args();
    instr.destroy();
    // TODO("handle cond branch being constant or skip top\n");
    values.insert({replacement_term.result(), ConstantValue::Top()});
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
    if (a.is_const() && a.is_global()) {
      return ConstantValue::Bottom();
    }
    if (a.is_const() && a.is_poison()) {
      return a;
    }
    switch (static_cast<fir::ConversionSubType>(instr->get_instr_subtype())) {
    case fir::ConversionSubType::INVALID:
      UNREACH();
    case fir::ConversionSubType::BitCast: {
      // fmt::println("Doing conversion================");
      auto *buf = utils::TempAlloc<u8>{}.allocate(a.vtype->get_size());
      // fmt::println("{:cd} ", a.toConstantValue(ctx, a.vtype).value());
      if (!a.storeConstant(buf, a.vtype)) {
        return ConstantValue::Top();
      }
      auto r = ConstantValue::loadConstant(buf, instr->result_type(), ctx);
      if (!r) {
        return ConstantValue::Top();
      }
      // fmt::println(
      //     "{:cd} ",
      //     r.value().toConstantValue(ctx, instr->get_type()).value());
      // fmt::println("\nDONE================");
      return r.value();
    }
    case fir::ConversionSubType::FPTOUI:
      ASSERT(a.vals.size() <= 1);
      if (a.vtype->get_bitwidth() == 32) {
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<u32>(a.as_f32()), instr->result_type()));
      }
      return ConstantValue::Constant(ctx->get_constant_value(
          static_cast<u64>(a.as_f64()), instr->result_type()));
    case fir::ConversionSubType::FPTOSI:
      ASSERT(a.vals.size() <= 1);
      if (a.vtype->get_bitwidth() == 32) {
        return ConstantValue::Constant(ctx->get_constant_value(
            static_cast<i32>(a.as_f32()), instr->result_type()));
      }
      return ConstantValue::Constant(ctx->get_constant_value(
          static_cast<i64>(a.as_f64()), instr->result_type()));
    case fir::ConversionSubType::UITOFP:
    case fir::ConversionSubType::SITOFP: {
      ASSERT(a.vals.size() <= 1);
      const bool is_signed = static_cast<fir::ConversionSubType>(
                                 instr->get_instr_subtype()) ==
                             fir::ConversionSubType::SITOFP;
      const APInt ai = a.as_apint();
      if (ai.bits() > 128) {
        return ConstantValue::Bottom();
      }
      switch (instr->result_type()->as_float()) {
        case 32:
          return ConstantValue::Constant(ctx->get_constant_value(
              is_signed ? static_cast<f32>(ai.to_i128())
                        : static_cast<f32>(ai.to_u128()),
              instr->result_type()));
        case 64:
          return ConstantValue::Constant(ctx->get_constant_value(
              is_signed ? static_cast<f64>(ai.to_i128())
                        : static_cast<f64>(ai.to_u128()),
              instr->result_type()));
        default:
          TODO("dont suport other bitwidths");
      }
    }
    case fir::ConversionSubType::PtrToInt:
    case fir::ConversionSubType::IntToPtr:
      ASSERT(a.vals.size() <= 1);
      return ConstantValue::Constant(ctx->intern_constant(fir::ConstantValue(
          a.as_apint().zext_or_trunc(instr->result_type()->get_bitwidth()),
          instr->result_type())));
    case fir::ConversionSubType::FPEXT:
      ASSERT(a.vals.size() <= 1);
      ASSERT(a.is_float() && a.get_type()->get_bitwidth() == 32);
      return ConstantValue::Constant(ctx->get_constant_value(
          static_cast<f64>(a.as_f32()), instr->result_type()));
    case fir::ConversionSubType::FPTRUNC:
      ASSERT(a.vals.size() <= 1);
      return ConstantValue::Constant(ctx->get_constant_value(
          static_cast<f32>(a.as_f64()), instr->result_type()));
    }
  }
  case fir::InstrType::ExtractElement: {
    auto v = eval(instr->get_arg(0));
    auto indx = eval(instr->get_arg(1));
    if (v.is_bottom() || indx.is_bottom()) {
      return ConstantValue::Bottom();
    }
    if (v.is_top() || indx.is_top()) {
      return ConstantValue::Top();
    }
    if (v.is_poison() || indx.is_poison()) {
      return ConstantValue::Constant(
          ctx->get_poisson_value(instr->result_type()));
    }
    ASSERT(v.is_const());
    ASSERT(indx.is_int());
    if (v.vtype->is_vec()) {
      ASSERT(v.vals.size() > 1);
      const u64 lane = indx.as_apint().to_u64();
      ASSERT(v.vals.size() > lane);
      auto r = v.vals[lane];
      auto t = v.vtype->as_vec();
      if (t.type == fir::VectorType::SubType::Floating) {
        return ConstantValue::Constant(r.f, ctx->get_float_type(t.bitwidth));
      }
      return ConstantValue::Constant(r.i, ctx->get_int_type(t.bitwidth));
    }
    return ConstantValue::Bottom();
  }
  case fir::InstrType::InsertElement:
    return ConstantValue::Bottom();
  case fir::InstrType::ZExt: {
    auto a = eval(instr->get_arg(0));
    if (a.is_bottom()) {
      return ConstantValue::Bottom();
    }
    if (a.is_top()) {
      return ConstantValue::Top();
    }
    if (a.is_poison()) {
      return ConstantValue::Constant(
          ctx->get_poisson_value(instr->result_type()));
    }
    if (a.vals.size() == 1) {
      return ConstantValue::Constant(ctx->intern_constant(fir::ConstantValue(
          a.as_apint().zext_or_trunc(instr->result_type()->get_bitwidth()),
          instr->result_type())));
    }
    IRVec<fir::ConstantValueR> args;
    for (const auto &v : a.vals) {
      args.push_back(ctx->get_constant_int(
          v.i.zext_or_trunc(instr->result_type()->as_vec().bitwidth)));
    }
    return ConstantValue::Constant(
        ctx->get_constant_value(args, instr.result_type()));
  }
  case fir::InstrType::SExt: {
    auto a = eval(instr->get_arg(0));
    if (a.is_bottom()) {
      return ConstantValue::Bottom();
    }
    if (a.is_top()) {
      return ConstantValue::Top();
    }
    size_t old_width = 0;
    size_t new_width = 0;
    if (a.vals.size() == 1) {
      old_width = a.get_type()->get_bitwidth();
      new_width = instr->result_type()->get_bitwidth();
    } else {
      ASSERT(instr.result_type()->is_vec());
      old_width = a.get_type()->as_vec().bitwidth;
      new_width = instr->result_type()->as_vec().bitwidth;
    }
    (void)old_width;
    auto const_prop = [new_width](const APInt &old_value) {
      return old_value.sext_or_trunc(static_cast<u32>(new_width));
    };

    ASSERT(a.is_int());
    ConstantValue res;
    res.vtype = instr->result_type();
    res.type = ConstantValue::ValueType::Int;
    res.vals.clear();
    for (auto &val : a.vals) {
      res.vals.push_back(ConstantValue::Value{.i = const_prop(val.i)});
    }
    return res;
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
    if (a.is_poison() || b.is_poison()) {
      return ConstantValue::Constant(
          ctx->get_poisson_value(instr->result_type()));
    }
    ASSERT(a.vals.size() == b.vals.size());
    auto res_type = ctx->get_int_type(1);
    if (a.vals.size() != 1) {
      res_type = ctx->get_vec_type(fir::VectorType::SubType::Integer, 1,
                                   a.vals.size());
    }

    TVec<ConstantValue::Value> res_vals;

    for (size_t i = 0; i < a.vals.size(); i++) {
      switch (static_cast<fir::FCmpInstrSubType>(instr->get_instr_subtype())) {
      case fir::FCmpInstrSubType::IsNaN:
        if (a.get_type()->as_float() == 32) {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(std::isnan(a.as_f32(i)))})});
          break;
        } else {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(std::isnan(a.as_f64(i)))})});
          break;
        }
      case fir::FCmpInstrSubType::OGT:
        if (a.get_type()->as_float() == 32) {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f32(i) > b.as_f32(i))})});
          break;
        } else {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f64(i) > b.as_f64(i))})});
          break;
        }
      case fir::FCmpInstrSubType::OLT:
        if (a.get_type()->as_float() == 32) {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f32(i) < b.as_f32(i))})});
          break;
        } else {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f64(i) < b.as_f64(i))})});
          break;
        }
      case fir::FCmpInstrSubType::OEQ:
        if (a.get_type()->as_float() == 32) {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f32(i) == b.as_f32(i))})});
          break;
        } else {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f64(i) == b.as_f64(i))})});
          break;
        }
      case fir::FCmpInstrSubType::OGE:
        if (a.get_type()->as_float() == 32) {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f32(i) >= b.as_f32(i))})});
          break;
        } else {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f64(i) >= b.as_f64(i))})});
          break;
        }
      case fir::FCmpInstrSubType::OLE:
        if (a.get_type()->as_float() == 32) {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f32(i) <= b.as_f32(i))})});
          break;
        } else {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f64(i) <= b.as_f64(i))})});
          break;
        }
      case fir::FCmpInstrSubType::ONE:
        if (a.get_type()->as_float() == 32) {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f32(i) != b.as_f32(i))})});
          break;
        } else {
          res_vals.push_back(ConstantValue::Value{
              .i = APInt(1, u64{static_cast<bool>(a.as_f64(i) != b.as_f64(i))})});
          break;
        }
      case fir::FCmpInstrSubType::UEQ:
      case fir::FCmpInstrSubType::UGT:
      case fir::FCmpInstrSubType::UGE:
      case fir::FCmpInstrSubType::ULT:
      case fir::FCmpInstrSubType::ULE:
      case fir::FCmpInstrSubType::UNE:
      case fir::FCmpInstrSubType::ORD:
      case fir::FCmpInstrSubType::UNO: {
        // f32 -> f64 is exact so comparing as double is equivalent
        const bool is32 = a.get_type()->as_float() == 32;
        const double x = is32 ? static_cast<double>(a.as_f32(i)) : a.as_f64(i);
        const double y = is32 ? static_cast<double>(b.as_f32(i)) : b.as_f64(i);
        const bool unordered = std::isnan(x) || std::isnan(y);
        bool r = false;
        switch (static_cast<fir::FCmpInstrSubType>(
            instr->get_instr_subtype())) {
        case fir::FCmpInstrSubType::UEQ:
          r = unordered || x == y;
          break;
        case fir::FCmpInstrSubType::UGT:
          r = unordered || x > y;
          break;
        case fir::FCmpInstrSubType::UGE:
          r = unordered || x >= y;
          break;
        case fir::FCmpInstrSubType::ULT:
          r = unordered || x < y;
          break;
        case fir::FCmpInstrSubType::ULE:
          r = unordered || x <= y;
          break;
        case fir::FCmpInstrSubType::UNE:
          r = unordered || x != y;
          break;
        case fir::FCmpInstrSubType::ORD:
          r = !unordered;
          break;
        default:
          r = unordered;
          break;
        }
        res_vals.push_back(ConstantValue::Value{.i = APInt(1, u64{r})});
        break;
      }
      case fir::FCmpInstrSubType::AlwFalse:
        res_vals.push_back(ConstantValue::Value{.i = APInt(1, u64{0})});
        break;
      case fir::FCmpInstrSubType::AlwTrue:
        res_vals.push_back(ConstantValue::Value{.i = APInt(1, u64{1})});
        break;
      case fir::FCmpInstrSubType::INVALID:
        UNREACH();
      }
    }
    return ConstantValue{.type = ConstantValue::ValueType::Int,
                         .vals = res_vals,
                         .vtype = res_type};
  }
  case fir::InstrType::ICmp: {
    auto a = eval(instr->get_arg(0));
    auto b = eval(instr->get_arg(1));

    const auto res_type = ctx->get_int_type(1);
    if (a.is_const() && a.vals.size() == 1 &&
        static_cast<fir::ICmpInstrSubType>(instr->get_instr_subtype()) ==
            fir::ICmpInstrSubType::ULE) {
      if (a.as_apint().is_zero()) {
        return ConstantValue::Constant(
            ctx->get_constant_value(static_cast<u64>(1), res_type));
      }
    } else if (a.is_const() && a.vals.size() == 1 &&
               static_cast<fir::ICmpInstrSubType>(instr->get_instr_subtype()) ==
                   fir::ICmpInstrSubType::UGT) {
      if (a.as_apint().is_zero()) {
        return ConstantValue::Constant(
            ctx->get_constant_value(static_cast<u64>(0), res_type));
      }
    }

    if (a.is_bottom() || b.is_bottom()) {
      return ConstantValue::Bottom();
    }
    if (!a.is_const() || !b.is_const()) {
      return ConstantValue::Top();
    }
    if (a.is_poison() || b.is_poison()) {
      return ConstantValue::Poision(instr->result_type());
    }

    size_t bit_width = 0;
    if (a.vals.size() == 1) {
      ASSERT(instr.result_type()->is_int() || instr.result_type()->is_ptr());
      bit_width = std::max(instr->args[0].get_type()->get_bitwidth(),
                           instr->args[1].get_type()->get_bitwidth());
    } else {
      ASSERT(instr.result_type()->is_vec());
      bit_width = std::max(instr->args[0].get_type()->as_vec().bitwidth,
                           instr->args[1].get_type()->as_vec().bitwidth);
    }
    const u32 bw = static_cast<u32>(bit_width);
    auto instr_ty = instr->get_instr_subtype();

    auto constnat_prop = [bw, instr_ty](const APInt &a_in,
                                        const APInt &b_in) -> u64 {
      const APInt a_val = a_in.sext_or_trunc(bw);
      const APInt b_val = b_in.sext_or_trunc(bw);
      switch (static_cast<fir::ICmpInstrSubType>(instr_ty)) {
      case fir::ICmpInstrSubType::INVALID:
        UNREACH();
        break;
      case fir::ICmpInstrSubType::NE:
        return static_cast<u64>(a_val != b_val);
      case fir::ICmpInstrSubType::EQ:
        return static_cast<u64>(a_val == b_val);
      case fir::ICmpInstrSubType::SLT:
        return static_cast<u64>(a_val.slt(b_val));
      case fir::ICmpInstrSubType::ULT:
        return static_cast<u64>(a_val.ult(b_val));
      case fir::ICmpInstrSubType::SGT:
        return static_cast<u64>(a_val.sgt(b_val));
      case fir::ICmpInstrSubType::UGT:
        return static_cast<u64>(a_val.ugt(b_val));
      case fir::ICmpInstrSubType::UGE:
        return static_cast<u64>(a_val.uge(b_val));
      case fir::ICmpInstrSubType::ULE:
        return static_cast<u64>(a_val.ule(b_val));
      case fir::ICmpInstrSubType::SGE:
        return static_cast<u64>(a_val.sge(b_val));
      case fir::ICmpInstrSubType::SLE:
        return static_cast<u64>(a_val.sle(b_val));
      }
      UNREACH();
    };

    ConstantValue res;
    res.vtype = res_type;
    res.type = ConstantValue::ValueType::Int;
    res.vals.clear();
    for (size_t i = 0; i < a.vals.size(); i++) {
      res.vals.push_back(
          ConstantValue::Value{
          .i = APInt(1, constnat_prop(a.as_apint(i), b.as_apint(i)))});
    }
    return res;
  }
  case fir::InstrType::ITrunc: {
    auto a = eval(instr->get_arg(0));
    ASSERT(a.vals.size() <= 1);

    if (a.is_bottom()) {
      return ConstantValue::Bottom();
    }
    if (a.is_top()) {
      return ConstantValue::Top();
    }

    if (!a.is_int()) {
      failure({.reason = "Impl icmp on non ints", .loc = instr});
      return ConstantValue::Bottom();
    }

    const u32 res_type_width = instr->result_type()->get_bitwidth();
    return ConstantValue::Constant(
        ctx->get_constant_int(a.as_apint().zext_or_trunc(res_type_width)));
  }
  case fir::InstrType::SelectInstr: {
    auto c = eval(instr->get_arg(0));
    auto a = eval(instr->get_arg(1));
    auto b = eval(instr->get_arg(2));
    ASSERT(c.vals.size() <= 1);
    if (a.vals.size() > 1 || b.vals.size() > 1) {
      // vector operands are not folded
      return ConstantValue::Bottom();
    }

    if (c.is_bottom()) {
      return ConstantValue::Bottom();
    }
    if (c.is_top()) {
      return ConstantValue::Top();
    }
    if (c.is_poison()) {
      TODO("okeee");
      // TODO: idk if this makes the most sense
      return a;
    }

    if (!c.as_apint().is_zero()) {
      return a;
    }
    return b;
  }
  case fir::InstrType::LoadInstr: {
    auto c = eval(instr->get_arg(0));
    if (c.is_bottom()) {
      return ConstantValue::Bottom();
    }
    if (c.is_top() || !c.is_global()) {
      return ConstantValue::Top();
    }
    if (!c.vals[0].gptr->is_constant ||
        (c.vals[0].gptr->linkage != fir::Linkage::Internal &&
         c.vals[0].gptr->linkage != fir::Linkage::LinkOnceODR)) {
      return ConstantValue::Top();
    }
    ASSERT(c.vals.size() <= 1);
    auto ty = instr->result_type();
    auto glob = c.vals[0].gptr;
    if (!glob->reloc_info.empty()) {
      return ConstantValue::Top();
    }
    auto res = ConstantValue::loadConstant(glob->init_value, ty, ctx);
    if (res) {
      return res.value();
    }
    fmt::println("{:cd}", instr);
    TODO("impl");
  }
  case fir::InstrType::CallInstr:
  case fir::InstrType::AllocaInstr:
  case fir::InstrType::ReturnInstr:
  case fir::InstrType::Unreachable:
  case fir::InstrType::StoreInstr:
    return ConstantValue::Top();
  }
  assert(false);
}

} // namespace foptim::optim
