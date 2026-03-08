#pragma once
#include <fmt/base.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <deque>

#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/arena.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

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
      Float,
      Int,
      Gptr,
      Fptr,
      NullPtr,
      Poison,
      Bottom,
    };

    enum class ConstantType {};
    union Value {
      f64 f;
      i128 i;
      fir::Global gptr;
      fir::FunctionR fptr;
    };
    ValueType type;
    TVec<Value> vals;
    fir::TypeR vtype;

    [[nodiscard]] constexpr fir::TypeR get_type() const { return vtype; }
    [[nodiscard]] constexpr bool is_top() const {
      return type == ValueType::Top;
    }
    [[nodiscard]] constexpr bool is_bottom() const {
      return type == ValueType::Bottom;
    }
    [[nodiscard]] constexpr bool is_const() const {
      return type != ValueType::Top && type != ValueType::Bottom;
    }
    [[nodiscard]] constexpr bool is_poison() const {
      return type == ValueType::Poison;
    }
    [[nodiscard]] constexpr bool is_global() const {
      return type == ValueType::Gptr;
    }
    [[nodiscard]] constexpr bool is_null() const {
      return type == ValueType::NullPtr;
    }
    [[nodiscard]] constexpr bool is_int() const {
      return type == ValueType::Int;
    }
    [[nodiscard]] constexpr bool is_float() const {
      return type == ValueType::Float;
    }
    [[nodiscard]] constexpr f32 as_f32() const {
      return std::bit_cast<f32>((u32)std::bit_cast<u64>(vals.at(0).f));
    }
    [[nodiscard]] constexpr f64 as_f64() const { return vals.at(0).f; }
    [[nodiscard]] constexpr i128 as_int() const {
      if (type == ValueType::NullPtr) {
        return 0;
      } else {
        return vals.at(0).i;
      }
    }

    static ConstantValue Top() {
      return ConstantValue{.type = ValueType::Top, .vals = {}, .vtype = {}};
    }
    static ConstantValue Bottom() {
      return ConstantValue{.type = ValueType::Bottom, .vals = {}, .vtype = {}};
    }
    static ConstantValue Constant(fir::ConstantValueR v) {
      auto c = v->get_type();
      if (v->is_poison()) {
        return ConstantValue{
            .type = ValueType::Poison, .vals = {}, .vtype = v->get_type()};
      }
      if (v->is_null()) {
        return ConstantValue{.type = ValueType::NullPtr,
                             .vals = {{.i = 0}},
                             .vtype = v->get_type()};
      }
      if (c->is_float()) {
        return ConstantValue{.type = ValueType::Float,
                             .vals = {{.f = v->as_float()}},
                             .vtype = v->get_type()};
      }
      if (c->is_int() || (c->is_ptr() && v->is_int())) {
        return ConstantValue{.type = ValueType::Int,
                             .vals = {{.i = v->as_int()}},
                             .vtype = v->get_type()};
      }
      if (v->is_global()) {
        return ConstantValue{.type = ValueType::Gptr,
                             .vals = {{.gptr = v->as_global()}},
                             .vtype = v->get_type()};
      }
      if (v->is_func()) {
        return ConstantValue{.type = ValueType::Fptr,
                             .vals = {{.fptr = v->as_func()}},
                             .vtype = v->get_type()};
      }
      if (v->is_vec()) {
        const auto &tv = v->as_vec();
        auto r = ConstantValue{
            .type = ValueType::Poison, .vals = {}, .vtype = v->get_type()};

        if (tv.members[0]->is_float()) {
          r.type = ValueType::Float;
        } else {
          r.type = ValueType::Int;
        }
        for (auto m : tv.members) {
          if (m->is_float()) {
            r.vals.push_back({.f = m->as_float()});
          } else if (m->is_int()) {
            r.vals.push_back({.i = m->as_int()});
          }
        }
        return r;
      }
      fmt::println("{:cd}", v);
      TODO("impl");
    }
    std::optional<fir::ConstantValueR> toConstantValue(fir::Context ctx,
                                                       fir::TypeR t) {
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
                args.push_back(ctx->get_constant_value(
                    v.f, ctx->get_float_type(elem_width)));
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

    static ConstantValue Constant(f32 v, fir::TypeR t) {
      return ConstantValue{
          .type = ValueType::Float,
          .vals = {{.f = std::bit_cast<f64>((u64)std::bit_cast<u32>(v))}},
          .vtype = t};
    }
    static ConstantValue Constant(f64 v, fir::TypeR t) {
      return ConstantValue{
          .type = ValueType::Float, .vals = {{.f = v}}, .vtype = t};
    }
    static ConstantValue Constant(i128 v, fir::TypeR t) {
      return ConstantValue{
          .type = ValueType::Int, .vals = {{.i = v}}, .vtype = t};
    }
    static std::optional<ConstantValue> loadConstant(u8 *v, fir::TypeR c,
                                                     fir::Context &ctx) {
      auto bitwidth = c->get_bitwidth();
      if (c->is_float() && bitwidth == 32) {
        return ConstantValue{.type = ValueType::Float,
                             .vals = {{.f = std::bit_cast<f64>((
                                           u64)std::bit_cast<u32>(*(f32 *)v))}},
                             .vtype = c};
      }
      if (c->is_float() && bitwidth == 64) {
        return ConstantValue{
            .type = ValueType::Float, .vals = {{.f = *(f64 *)v}}, .vtype = c};
      }
      if ((c->is_int() && bitwidth == 64) || c->is_ptr()) {
        auto val = (*(u64 *)v);
        if (c->is_ptr() && val == 0) {
          return ConstantValue{.type = ValueType::NullPtr,
                               .vals = {{.i = 0}},
                               .vtype = c};
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
        return ConstantValue{
            .type = ValueType::Int,
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

    bool storeConstant(u8 *v, fir::TypeR c) {
      auto bitwidth = c->get_bitwidth();
      if (c->is_float() && bitwidth == 32) {
        *((f32 *)v) = (f32)vals[0].f;
        return true;
      }
      if (c->is_float() && bitwidth == 64) {
        *((f64 *)v) = vals[0].f;
        return true;
      }
      if (c->is_int() && bitwidth == 8) {
        *((i8 *)v) = (i8)vals[0].i;
        return true;
      }
      if ((c->is_ptr() || c->is_int()) && bitwidth == 64) {
        *((i64 *)v) = (i64)vals[0].i;
        return true;
      }
      if (c->is_vec()) {
        auto cv = c->as_vec();
        for (size_t i = 0; i < vals.size(); i++) {
          if (cv.type == fir::VectorType::SubType::Floating &&
              cv.bitwidth == 32) {
            *(((f32 *)(v + (i * cv.bitwidth / 8)))) = (f32)vals[i].f;
          } else if (cv.type == fir::VectorType::SubType::Integer &&
                     cv.bitwidth == 32) {
            *(((i32 *)(v + (i * cv.bitwidth / 8)))) = (i32)vals[i].i;
          } else if (cv.type == fir::VectorType::SubType::Integer &&
                     cv.bitwidth == 64) {
            *(((u64 *)(v + (i * cv.bitwidth / 8)))) = (i64)vals[i].i;
          } else {
            fmt::println("Data store {:cd}", c);
            TODO("impl");
          }
        }
        return true;
      }
      fmt::println("Data store {:cd}", c);
      TODO("impl");
    }

    bool operator==(const ConstantValue &other) const {
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
      case fir::InstrType::Fence: {
        return ConstantValue::Bottom();
      }
      case fir::InstrType::AtomicRMW: {
        return ConstantValue::Top();
      }
      case fir::InstrType::VectorInstr: {
        switch ((fir::VectorISubType)instr->get_instr_subtype()) {
          case fir::VectorISubType::HorizontalAdd: {
            auto a = eval(instr->get_arg(0));
            if (a.is_bottom()) {
              return ConstantValue::Bottom();
            }
            if (!a.is_const() && (!a.is_int() && !a.is_float())) {
              return ConstantValue::Top();
            }

            if (a.is_int()) {
              TODO("need to handle widths??");
            } else if (a.is_float() &&
                       ((a.vtype->is_float() && a.vtype->as_float() == 64) ||
                        (a.is_float() && a.vtype->is_vec() &&
                         a.vtype->as_vec().bitwidth == 64))) {
              f64 res = 0;
              for (auto &m : a.vals) {
                res += m.f;
              }
              return ConstantValue::Constant(res, instr->get_type());
            } else if (a.is_float() &&
                       ((a.vtype->is_float() && a.vtype->as_float() == 32) ||
                        (a.is_float() && a.vtype->is_vec() &&
                         a.vtype->as_vec().bitwidth == 32))) {
              f32 res = 0;
              for (auto &m : a.vals) {
                res += std::bit_cast<f32>((u32)std::bit_cast<u64>(m.f));
              }
              return ConstantValue::Constant(res, instr->get_type());
            }
            return ConstantValue::Top();
          }
          case fir::VectorISubType::INVALID:
          case fir::VectorISubType::Broadcast:
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
              {.reason =
                   "Cannot do SCCP on binary expr using non integers/floats",
               .loc = instr});
          return ConstantValue::Bottom();
        }

        auto out_type = a.get_type();

        switch ((fir::UnaryInstrSubType)instr->get_instr_subtype()) {
          case fir::UnaryInstrSubType::INVALID:
            UNREACH();
          case fir::UnaryInstrSubType::FloatNeg:
            if (out_type->as_float() == 32) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(-a.as_f32(), out_type));
            } else {
              return ConstantValue::Constant(
                  ctx->get_constant_value(-a.as_f64(), out_type));
            }
          case fir::UnaryInstrSubType::IntNeg:
            return ConstantValue::Constant(
                ctx->get_constant_value(-a.as_int(), out_type));
          case fir::UnaryInstrSubType::Not: {
            // return ConstantValue::Bottom();
            auto mask = ((i128)1 << out_type->as_int()) - 1;
            return ConstantValue::Constant(
                ctx->get_constant_value((~a.as_int()) & mask, out_type));
          }
          default:
            fmt::println("{}", instr);
            IMPL("implement instr");
            UNREACH();
        }
      }
      case fir::InstrType::Intrinsic: {
        bool is_signed = false;
        // TODO: implement constant propagation
        switch ((fir::IntrinsicSubType)instr->get_instr_subtype()) {
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
                          (a.vtype->is_vec() &&
                           a.vtype->as_vec().bitwidth == 32))) {
                m.f = std::bit_cast<f64>((u64)std::bit_cast<u32>(std::abs(
                    std::bit_cast<f32>((u32)std::bit_cast<u64>(m.f)))));
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
              m.i = std::abs(m.i);
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
              m.i = 1;
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
            if ((!a.is_const() || !a.is_int()) ||
                (!b.is_const() || !b.is_int())) {
              return ConstantValue::Top();
            }
            for (size_t i = 0; i < a.vals.size(); i++) {
              if (is_signed) {
                a.vals[i].i = std::max(std::bit_cast<i128>(a.vals[i].i),
                                       std::bit_cast<i128>(b.vals[i].i));
              } else {
                a.vals[i].i = std::max(std::bit_cast<u128>(a.vals[i].i),
                                       std::bit_cast<u128>(b.vals[i].i));
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
            if ((!a.is_const() || !a.is_int()) ||
                (!b.is_const() || !b.is_int())) {
              return ConstantValue::Top();
            }
            for (size_t i = 0; i < a.vals.size(); i++) {
              if (is_signed) {
                a.vals[i].i = std::min(std::bit_cast<i128>(a.vals[i].i),
                                       std::bit_cast<i128>(b.vals[i].i));
              } else {
                a.vals[i].i = std::min(std::bit_cast<u128>(a.vals[i].i),
                                       std::bit_cast<u128>(b.vals[i].i));
              }
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
              a.vals[i].i = std::min(a.vals[i].f, b.vals[i].f);
              // if (a.vtype->as_float() == 32) {
              // } else {
              //   a.vals[i].i = std::max((a.vals[i].f), (b.vals[i].f));
              // }
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
              a.vals[i].i = std::max(a.vals[i].f, b.vals[i].f);
              // if (a.vtype->as_float() == 32) {
              // } else {
              //   a.vals[i].i = std::max((a.vals[i].f), (b.vals[i].f));
              // }
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
                if (a.vals[0].i == 0) {
                  return ConstantValue::Top();
                }
              }
            }
            for (size_t i = 0; i < a.vals.size(); i++) {
              a.vals[i].i = __builtin_clzg(std::bit_cast<u128>(a.vals[i].i));
            }
            return a;
          }
          case fir::IntrinsicSubType::INVALID:
          case fir::IntrinsicSubType::VA_start:
          case fir::IntrinsicSubType::VA_end:
            break;
        }
        return ConstantValue::Bottom();
      }
      case fir::InstrType::BinaryInstr: {
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
              {.reason =
                   "Cannot do SCCP on binary expr using non integers/floats",
               .loc = instr});
          return ConstantValue::Top();
        }
        ASSERT(a.vals.size() <= 1);
        ASSERT(b.vals.size() <= 1);

        auto out_type = a.get_type();

        switch ((fir::BinaryInstrSubType)instr->get_instr_subtype()) {
          default:
            fmt::println("{}", instr);
            IMPL("implement instr");
            UNREACH();
          case fir::BinaryInstrSubType::INVALID:
            UNREACH();
          case fir::BinaryInstrSubType::And:
            return ConstantValue::Constant(
                ctx->get_constant_value(a.as_int() & b.as_int(), out_type));
          case fir::BinaryInstrSubType::Xor:
            return ConstantValue::Constant(
                ctx->get_constant_value(a.as_int() ^ b.as_int(), out_type));
          case fir::BinaryInstrSubType::Or:
            return ConstantValue::Constant(
                ctx->get_constant_value(a.as_int() | b.as_int(), out_type));
          case fir::BinaryInstrSubType::Shl:
            return ConstantValue::Constant(ctx->get_constant_value(
                (a.as_int() << b.as_int()) &
                    (((i128)1 << out_type->as_int()) - 1),
                out_type));
          case fir::BinaryInstrSubType::Shr:
            return ConstantValue::Constant(ctx->get_constant_value(
                (std::bit_cast<i128>(std::bit_cast<u128>(a.as_int()) >>
                                     std::bit_cast<u128>(b.as_int())) &
                 (((i128)1 << out_type->as_int()) - 1)),
                out_type));
          case fir::BinaryInstrSubType::IntSub:
            return ConstantValue::Constant(
                ctx->get_constant_value(a.as_int() - b.as_int(), out_type));
          case fir::BinaryInstrSubType::IntAdd:
            return ConstantValue::Constant(
                ctx->get_constant_value(a.as_int() + b.as_int(), out_type));
          case fir::BinaryInstrSubType::IntUDiv:
            return ConstantValue::Constant(ctx->get_constant_value(
                (u64)a.as_int() / (u64)b.as_int(), out_type));
          case fir::BinaryInstrSubType::IntSDiv: {
            auto a_width = (128 - a.get_type()->get_bitwidth());
            auto b_width = (128 - a.get_type()->get_bitwidth());
            auto sexta = (a.as_int() << a_width) >> a_width;
            auto sextb = (b.as_int() << b_width) >> b_width;
            return ConstantValue::Constant(
                ctx->get_constant_value(sexta / sextb, out_type));
          }
          case fir::BinaryInstrSubType::IntSRem: {
            auto a_width = (128 - a.get_type()->get_bitwidth());
            auto b_width = (128 - a.get_type()->get_bitwidth());
            auto sexta = (a.as_int() << a_width) >> a_width;
            auto sextb = (b.as_int() << b_width) >> b_width;
            return ConstantValue::Constant(
                ctx->get_constant_value(sexta % sextb, out_type));
          }
          case fir::BinaryInstrSubType::IntURem: {
            auto a_width = (128 - a.get_type()->get_bitwidth());
            auto b_width = (128 - a.get_type()->get_bitwidth());
            auto zexta =
                (std::bit_cast<u128>(a.as_int()) << a_width) >> a_width;
            auto zextb =
                (std::bit_cast<u128>(b.as_int()) << b_width) >> b_width;
            return ConstantValue::Constant(ctx->get_constant_value(
                std::bit_cast<i128>(zexta % zextb), out_type));
          }
          case fir::BinaryInstrSubType::IntMul:
            return ConstantValue::Constant(
                ctx->get_constant_value(a.as_int() * b.as_int(), out_type));
          case fir::BinaryInstrSubType::FloatAdd:
            if (out_type->as_float() == 32) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(a.as_f32() + b.as_f32(), out_type));
            } else if (out_type->as_float() == 64) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(a.as_f64() + b.as_f64(), out_type));
            } else {
              TODO("support other bitwidths");
            }
          case fir::BinaryInstrSubType::FloatMul:
            if (out_type->as_float() == 32) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(a.as_f32() * b.as_f32(), out_type));
            } else if (out_type->as_float() == 64) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(a.as_f64() * b.as_f64(), out_type));
            } else {
              TODO("support other bitwidths");
            }
          case fir::BinaryInstrSubType::FloatDiv:
            if (out_type->as_float() == 32) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(a.as_f32() / b.as_f32(), out_type));
            } else if (out_type->as_float() == 64) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(a.as_f64() / b.as_f64(), out_type));
            } else {
              TODO("support other bitwidths");
            }
          case fir::BinaryInstrSubType::FloatSub:
            if (out_type->as_float() == 32) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(a.as_f32() - b.as_f32(), out_type));
            } else if (out_type->as_float() == 64) {
              return ConstantValue::Constant(
                  ctx->get_constant_value(a.as_f64() - b.as_f64(), out_type));
            } else {
              TODO("support other bitwidths");
            }
        }
        UNREACH();
      }
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
          cond = arg.as_int() != 0;
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

        instr.clear_bbs();
        instr.clear_args();
        instr.destroy();
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
        if (a.is_const() && a.is_global()) {
          return ConstantValue::Bottom();
        }
        if (a.is_const() && a.is_poison()) {
          return a;
        }
        switch ((fir::ConversionSubType)instr->get_instr_subtype()) {
          case fir::ConversionSubType::INVALID:
            UNREACH();
          case fir::ConversionSubType::BitCast: {
            // fmt::println("Doing conversion================");
            auto *buf = utils::TempAlloc<u8>{}.allocate(a.vtype->get_size());
            // fmt::println("{:cd} ", a.toConstantValue(ctx, a.vtype).value());
            if (!a.storeConstant(buf, a.vtype)) {
              return ConstantValue::Top();
            }
            auto r = ConstantValue::loadConstant(buf, instr->get_type(), ctx);
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
                  static_cast<u32>(a.as_f32()), instr->get_type()));
            }
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>(a.as_f64()), instr->get_type()));
          case fir::ConversionSubType::FPTOSI:
            ASSERT(a.vals.size() <= 1);
            if (a.vtype->get_bitwidth() == 32) {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f32()), instr->get_type()));
            }
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<i64>(a.as_f64()), instr->get_type()));
          case fir::ConversionSubType::UITOFP:
          case fir::ConversionSubType::SITOFP:
            ASSERT(a.vals.size() <= 1);
            switch (instr->get_type()->as_float()) {
              case 32:
                return ConstantValue::Constant(ctx->get_constant_value(
                    static_cast<f32>(a.as_int()), instr->get_type()));
              case 64:
                return ConstantValue::Constant(ctx->get_constant_value(
                    static_cast<f64>(a.as_int()), instr->get_type()));
              default:
                IMPL("dont suport other bitwidths");
            }
          case fir::ConversionSubType::PtrToInt:
          case fir::ConversionSubType::IntToPtr:
            ASSERT(a.vals.size() <= 1);
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>(a.as_int()), instr->get_type()));
          case fir::ConversionSubType::FPEXT:
            ASSERT(a.vals.size() <= 1);
            return ConstantValue::Constant(
                ctx->get_constant_value(a.as_f64(), instr->get_type()));
          case fir::ConversionSubType::FPTRUNC:
            ASSERT(a.vals.size() <= 1);
            return ConstantValue::Constant(
                ctx->get_constant_value((f32)a.as_f64(), instr->get_type()));
        }
      }
      case fir::InstrType::ExtractValue: {
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
              ctx->get_poisson_value(instr->get_type()));
        }
        ASSERT(v.is_const());
        ASSERT(indx.is_int());
        if (v.vtype->is_vec()) {
          ASSERT(v.vals.size() > 1);
          ASSERT(v.vals.size() > indx.as_int());
          auto r = v.vals[indx.as_int()];
          auto t = v.vtype->as_vec();
          if (t.type == fir::VectorType::SubType::Floating) {
            return ConstantValue::Constant(r.f,
                                           ctx->get_float_type(t.bitwidth));
          }
          return ConstantValue::Constant(r.i, ctx->get_int_type(t.bitwidth));
        }
        return ConstantValue::Bottom();
      }
      case fir::InstrType::InsertValue:
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
              ctx->get_poisson_value(instr->get_type()));
        }
        if (a.vals.size() == 1) {
          return ConstantValue::Constant(
              ctx->get_constant_value(a.as_int(), instr->get_type()));
        }
        IRVec<fir::ConstantValueR> args;
        for (const auto &v : a.vals) {
          args.push_back(ctx->get_constant_value(
              v.i, ctx->get_int_type(instr->get_type()->as_vec().bitwidth)));
        }
        return ConstantValue::Constant(
            ctx->get_constant_value(args, instr.get_type()));
      }
      case fir::InstrType::SExt: {
        auto a = eval(instr->get_arg(0));
        ASSERT(a.vals.size() <= 1);
        if (a.is_bottom()) {
          return ConstantValue::Bottom();
        }
        if (a.is_top()) {
          return ConstantValue::Top();
        }
        auto old_width = a.get_type()->as_int();
        auto old_value = a.as_int();
        auto is_negative = (old_value & ((i128)1 << (old_width - 1))) != 0;
        if (is_negative) {
          auto new_width = instr->get_type()->as_int();
          auto mask = ((i128)1 << (new_width - old_width)) - 1;
          mask = mask << old_width;
          return ConstantValue::Constant(
              ctx->get_constant_value(old_value | mask, instr->get_type()));
        }
        return ConstantValue::Constant(
            ctx->get_constant_value(old_value, instr->get_type()));
      }
      case fir::InstrType::FCmp: {
        auto a = eval(instr->get_arg(0));
        auto b = eval(instr->get_arg(1));
        ASSERT(a.vals.size() <= 1);
        ASSERT(b.vals.size() <= 1);
        if (a.is_bottom() || b.is_bottom()) {
          return ConstantValue::Bottom();
        }
        if (!a.is_const() || !b.is_const()) {
          return ConstantValue::Top();
        }
        if (a.is_poison() || b.is_poison()) {
          return ConstantValue::Constant(
              ctx->get_poisson_value(instr->get_type()));
        }
        const auto res_type = ctx->get_int_type(1);

        switch ((fir::FCmpInstrSubType)instr->get_instr_subtype()) {
          case fir::FCmpInstrSubType::IsNaN:
            if (a.get_type()->as_float() == 32) {
              return ConstantValue::Constant(ctx->get_constant_value(
                  (i32)std::isnan(a.as_f32()), res_type));
            } else {
              return ConstantValue::Constant(ctx->get_constant_value(
                  (i32)std::isnan(a.as_f64()), res_type));
            }
          case fir::FCmpInstrSubType::OGT:
            if (a.get_type()->as_float() == 32) {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f32() > b.as_f32()), res_type));
            } else {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f64() > b.as_f64()), res_type));
            }
          case fir::FCmpInstrSubType::OLT:
            if (a.get_type()->as_float() == 32) {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f32() < b.as_f32()), res_type));
            } else {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f64() < b.as_f64()), res_type));
            }
          case fir::FCmpInstrSubType::OEQ:
            if (a.get_type()->as_float() == 32) {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f32() == b.as_f32()), res_type));
            } else {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f64() == b.as_f64()), res_type));
            }
          case fir::FCmpInstrSubType::OGE:
            if (a.get_type()->as_float() == 32) {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f32() >= b.as_f32()), res_type));
            } else {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f64() >= b.as_f64()), res_type));
            }
          case fir::FCmpInstrSubType::OLE:
            if (a.get_type()->as_float() == 32) {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f32() <= b.as_f32()), res_type));
            } else {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f64() <= b.as_f64()), res_type));
            }
          case fir::FCmpInstrSubType::ONE:
            if (a.get_type()->as_float() == 32) {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f32() != b.as_f32()), res_type));
            } else {
              return ConstantValue::Constant(ctx->get_constant_value(
                  static_cast<i32>(a.as_f64() != b.as_f64()), res_type));
            }
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
            return ConstantValue::Constant(
                ctx->get_constant_value(static_cast<i32>(false), res_type));
          case fir::FCmpInstrSubType::AlwTrue:
            return ConstantValue::Constant(
                ctx->get_constant_value(static_cast<i32>(true), res_type));
          case fir::FCmpInstrSubType::INVALID:
            break;
        }
        failure({.reason = "Imply fcmp", .loc = instr});
        return ConstantValue::Bottom();
      }
      case fir::InstrType::ICmp: {
        auto a = eval(instr->get_arg(0));
        auto b = eval(instr->get_arg(1));
        ASSERT(a.vals.size() <= 1);
        ASSERT(b.vals.size() <= 1);

        const auto res_type = ctx->get_int_type(1);
        if (a.is_const() && (fir::ICmpInstrSubType)instr->get_instr_subtype() ==
                                fir::ICmpInstrSubType::ULE) {
          if (a.as_int() == 0) {
            return ConstantValue::Constant(
                ctx->get_constant_value(static_cast<u64>(1), res_type));
          }
        } else if (a.is_const() &&
                   (fir::ICmpInstrSubType)instr->get_instr_subtype() ==
                       fir::ICmpInstrSubType::UGT) {
          if (a.as_int() == 0) {
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

        if (!a.is_int() || !b.is_int()) {
          failure({.reason = "Impl icmp on non ints", .loc = instr});
          return ConstantValue::Bottom();
        }
        auto bit_width = std::max(instr->args[0].get_type()->as_int(),
                                  instr->args[1].get_type()->as_int());
        auto mask = ((i128)1 << bit_width) - 1;
        auto rest_width = (128 - bit_width);

        auto a_val = ((a.as_int() & mask) << rest_width) >> rest_width;
        auto b_val = ((b.as_int() & mask) << rest_width) >> rest_width;
        switch ((fir::ICmpInstrSubType)instr->get_instr_subtype()) {
          case fir::ICmpInstrSubType::INVALID:
            UNREACH();
            break;
          case fir::ICmpInstrSubType::NE:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>(a_val != b_val), res_type));
          case fir::ICmpInstrSubType::EQ:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>(a_val == b_val), res_type));
          case fir::ICmpInstrSubType::SLT:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>((i64)a_val < (i64)b_val), res_type));
          case fir::ICmpInstrSubType::ULT:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>(std::bit_cast<u64>((i64)a_val) <
                                 std::bit_cast<u64>((i64)b_val)),
                res_type));
          case fir::ICmpInstrSubType::SGT:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>((i64)a_val > (i64)b_val), res_type));
          case fir::ICmpInstrSubType::UGT:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>(std::bit_cast<u64>((i64)a_val) >
                                 std::bit_cast<u64>((i64)b_val)),
                res_type));
          case fir::ICmpInstrSubType::UGE:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>(std::bit_cast<u64>((i64)a_val) >=
                                 std::bit_cast<u64>((i64)b_val)),
                res_type));
          case fir::ICmpInstrSubType::ULE:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>(std::bit_cast<u64>((i64)a_val) <=
                                 std::bit_cast<u64>((i64)b_val)),
                res_type));
          case fir::ICmpInstrSubType::SGE:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>((i64)a_val >= (i64)b_val), res_type));
          case fir::ICmpInstrSubType::SLE:
            return ConstantValue::Constant(ctx->get_constant_value(
                static_cast<u64>((i64)a_val <= (i64)b_val), res_type));
          case fir::ICmpInstrSubType::MulOverflow: {
            i128 output = a_val * b_val;
            auto bitwidth =
                std::max(a.get_type()->as_int(), b.get_type()->as_int());
            i128 mask = ~(((i128)1 << bitwidth) - 1);
            return ConstantValue::Constant(
                ctx->get_constant_value((u64)((output & mask) != 0), res_type));
          }
          case fir::ICmpInstrSubType::AddOverflow: {
            i128 output = a_val + b_val;
            auto bitwidth =
                std::max(a.get_type()->as_int(), b.get_type()->as_int());
            i128 mask = ~(((i128)1 << bitwidth) - 1);
            return ConstantValue::Constant(
                ctx->get_constant_value((u64)((output & mask) != 0), res_type));
          }
        }
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

        auto res_type_width = instr->get_type()->as_int();
        u64 mask = ((u64)1 << res_type_width) - 1;
        return ConstantValue::Constant(
            ctx->get_constant_int(a.as_int() & mask, res_type_width));
      }
      case fir::InstrType::SelectInstr: {
        auto c = eval(instr->get_arg(0));
        auto a = eval(instr->get_arg(1));
        auto b = eval(instr->get_arg(2));
        ASSERT(c.vals.size() <= 1);
        ASSERT(a.vals.size() <= 1);
        ASSERT(b.vals.size() <= 1);

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

        if (c.as_int() != 0) {
          return a;
        }
        return b;
      }
      case fir::InstrType::LoadInstr: {
        auto c = eval(instr->get_arg(0));
        ASSERT(c.vals.size() <= 1);
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
        auto ty = instr->get_type();
        auto glob = c.vals[0].gptr;
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

  void eval_and_update(fir::Context &ctx, fir::ValueR value) {
    (void)values;
    (void)cfg;

    ConstantValue new_value = ConstantValue::Top();

    if (value.is_constant()) {
      IMPL("constant\n");
    } else if (value.is_instr()) {
      new_value = eval_instr(ctx, value.as_instr());
      // fmt::print("{:cd} => ", value.as_instr());
      // auto v = new_value.toConstantValue(ctx, value.get_type());
      // if (v) {
      //   fmt::println("{:cd}", v.value());
      // } else {
      //   fmt::println("");
      // }

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
        }
        // value.replace_all_uses(fir::ValueR{new_value.value});
      }
    } else if (value.is_bb_arg()) {
      UNREACH();
    }
  }

  void dump() {
    TODO("REIMPL");
    // print << "DUMP SCCP: ";
    // for (auto &[val, consta] : values) {
    //   print << val << ": ";
    //   switch (consta.type) {
    //   case ConstantValue::ValueType::Top:
    //     print << "TOP\n";
    //     break;
    //   case ConstantValue::ValueType::Constant:
    //     print << consta.value << "\n";
    //     break;
    //   case ConstantValue::ValueType::Bottom:
    //     print << "BOT\n";
    //     break;
    //   }
    // }
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

  void execute(fir::Context &ctx) {
    for (auto &[val, consta] : values) {
      if (consta.is_const()) {
        fir::ValueR val_non_const = val;

        auto res_co = consta.toConstantValue(ctx, val.get_type());
        if (res_co) {
          // fmt::println("sccp> {:cd}", res_co.value());
          val_non_const.replace_all_uses(fir::ValueR{res_co.value()});
        }
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
    execute(ctx);
  }
};
}  // namespace foptim::optim
