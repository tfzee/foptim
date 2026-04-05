#include "sccp.hpp"

#include <fmt/base.h>

#include <type_traits>

#include "ir/constant_value.hpp"
#include "utils/todo.hpp"

namespace foptim::optim {

std::optional<fir::ConstantValueR> SCCP::ConstantValue::toConstantValue(
    fir::Context ctx, fir::TypeR t) {
  switch (type) {
    case ValueType::Top:
    case ValueType::Bottom:
      return {};
    case ValueType::Poison:
      return ctx->get_poisson_value(t);
    case ValueType::NullPtr:
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
                std::bit_cast<f32>((u32)std::bit_cast<u64>(v.f)),
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
        return ctx->get_constant_value(vals[0].i, t);
      } else {
        IRVec<fir::ConstantValueR> args;
        auto elem_width = t->as_vec().bitwidth;
        for (auto &v : vals) {
          args.push_back(
              ctx->get_constant_value(v.i, ctx->get_int_type(elem_width)));
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

std::optional<SCCP::ConstantValue> SCCP::ConstantValue::loadConstant(
    u8 *v, fir::TypeR c, fir::Context &ctx)

{
  auto bitwidth = c->get_bitwidth();
  if (c->is_float() && bitwidth == 32) {
    return ConstantValue{
        .type = ValueType::Float,
        .vals = {{.f = std::bit_cast<f64>((u64)std::bit_cast<u32>(*(f32 *)v))}},
        .vtype = c};
  }
  if (c->is_float() && bitwidth == 64) {
    return ConstantValue{
        .type = ValueType::Float, .vals = {{.f = *(f64 *)v}}, .vtype = c};
  }
  if ((c->is_int() && bitwidth == 64) || c->is_ptr()) {
    auto val = (*(u64 *)v);
    if (c->is_ptr() && val == 0) {
      return ConstantValue{
          .type = ValueType::NullPtr, .vals = {{.i = 0}}, .vtype = c};
    }
    return ConstantValue{.type = ValueType::Int,
                         .vals = {{.i = std::bit_cast<i128>((u128)val)}},
                         .vtype = c};
  }
  if (c->is_int() && bitwidth == 32) {
    return ConstantValue{
        .type = ValueType::Int,
        .vals = {{.i = std::bit_cast<i128>((u128)(*(u32 *)v))}},
        .vtype = c};
  }
  if (c->is_int() && bitwidth == 8) {
    return ConstantValue{.type = ValueType::Int,
                         .vals = {{.i = std::bit_cast<i128>((u128)(*(u8 *)v))}},
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

template <class T>
T const_eval_bin(fir::Instr instr, fir::TypeR out_type, T a, T b) {
  switch ((fir::BinaryInstrSubType)instr->get_instr_subtype()) {
    default:
      fmt::println("{}", instr);
      IMPL("implement instr");
      UNREACH();
    case fir::BinaryInstrSubType::INVALID:
      UNREACH();
    case fir::BinaryInstrSubType::And:
      if constexpr (std::is_same_v<T, f32>) {
        return std::bit_cast<f32>(std::bit_cast<u32>(a) &
                                  std::bit_cast<u32>(b));
      } else if constexpr (std::is_same_v<T, f64>) {
        return std::bit_cast<f64>(std::bit_cast<u64>(a) &
                                  std::bit_cast<u64>(b));
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
        return (a << b) & (((i128)1 << out_type->as_int()) - 1);
      }
    case fir::BinaryInstrSubType::Shr:
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        return (std::bit_cast<i128>(std::bit_cast<u128>(a) >>
                                    std::bit_cast<u128>(b)) &
                (((i128)1 << out_type->as_int()) - 1));
      }
    case fir::BinaryInstrSubType::AShr: {
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        return a >> b;
      }
    }
    case fir::BinaryInstrSubType::IntSub:
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        return a - b;
      }
    case fir::BinaryInstrSubType::IntAdd:
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        return a + b;
      }
    case fir::BinaryInstrSubType::IntUDiv: {
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        return (u64)a / (u64)b;
      }
    }
    case fir::BinaryInstrSubType::IntSDiv: {
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        auto a_width = (128 - out_type->get_bitwidth());
        auto b_width = (128 - out_type->get_bitwidth());
        auto sexta = (a << a_width) >> a_width;
        auto sextb = (b << b_width) >> b_width;
        return sexta / sextb;
      }
    }
    case fir::BinaryInstrSubType::IntSRem: {
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        auto a_width = (128 - out_type->get_bitwidth());
        auto b_width = (128 - out_type->get_bitwidth());
        auto sexta = (a << a_width) >> a_width;
        auto sextb = (b << b_width) >> b_width;
        return sexta % sextb;
      }
    }
    case fir::BinaryInstrSubType::IntURem: {
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        auto a_width = (128 - out_type->get_bitwidth());
        auto b_width = (128 - out_type->get_bitwidth());
        auto zexta = (std::bit_cast<u128>(a) << a_width) >> a_width;
        auto zextb = (std::bit_cast<u128>(b) << b_width) >> b_width;
        return std::bit_cast<i128>(zexta % zextb);
      }
    }
    case fir::BinaryInstrSubType::IntMul:
      if constexpr (std::is_floating_point_v<T>) {
        UNREACH();
      } else {
        return a * b;
      }
    case fir::BinaryInstrSubType::FloatAdd:
      if constexpr (std::is_same_v<T, f32>) {
        return a + b;
      } else if constexpr (std::is_same_v<T, f64>) {
        return a + b;
      } else {
        TODO("support other bitwidths");
      }
    case fir::BinaryInstrSubType::FloatMul:
      if constexpr (std::is_same_v<T, f32>) {
        return a * b;
      } else if constexpr (std::is_same_v<T, f64>) {
        return a * b;
      } else {
        TODO("support other bitwidths");
      }
    case fir::BinaryInstrSubType::FloatDiv:
      if constexpr (std::is_same_v<T, f32>) {
        return a / b;
      } else if constexpr (std::is_same_v<T, f64>) {
        return a / b;
      } else {
        TODO("support other bitwidths");
      }
    case fir::BinaryInstrSubType::FloatSub:
      if constexpr (std::is_same_v<T, f32>) {
        return a - b;
      } else if constexpr (std::is_same_v<T, f64>) {
        return a - b;
      } else {
        TODO("support other bitwidths");
      }
  }
}

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
  if (a.is_int()) {
    for (size_t i = 0; i < a.vals.size(); i++) {
      auto c = ConstantValue::Value{
          .i = const_eval_bin(instr, out_type, a.as_int(i), b.as_int(i))};
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
            .f = std::bit_cast<f64>((u64)std::bit_cast<u32>(v))};
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

}  // namespace foptim::optim
