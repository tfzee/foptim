#pragma once
#include <fmt/base.h>

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
    // [[nodiscard]] constexpr bool is_f32() const {
    //   return type == ValueType::Float && vtype->as_float() == 32;
    // }
    // [[nodiscard]] constexpr bool is_f64() const {
    //   return type == ValueType::Float && vtype->as_float() == 64;
    // }
    [[nodiscard]] constexpr f32 as_f32(u32 idx = 0) const {
      return std::bit_cast<f32>((u32)std::bit_cast<u64>(vals.at(idx).f));
    }
    [[nodiscard]] constexpr f64 as_f64(u32 idx = 0) const {
      return vals.at(idx).f;
    }
    [[nodiscard]] constexpr i128 as_int(u32 idx = 0) const {
      if (type == ValueType::NullPtr) {
        return 0;
      }
      return vals.at(idx).i;
    }

    static ConstantValue Top() {
      return ConstantValue{.type = ValueType::Top, .vals = {}, .vtype = {}};
    }
    static ConstantValue Bottom() {
      return ConstantValue{.type = ValueType::Bottom, .vals = {}, .vtype = {}};
    }
    static ConstantValue Poision(fir::TypeR t) {
      return ConstantValue{.type = ValueType::Poison, .vals = {}, .vtype = t};
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
          } else if (m->is_int() || m->is_null()) {
            r.vals.push_back({.i = m->as_int()});
          } else if (m->is_global()) {
            r.vals.push_back({.gptr = m->as_global()});
          } else {
            fmt::println("{}", m);
            TODO("impl dufus");
          }
        }
        return r;
      }
      fmt::println("{:cd}", v);
      TODO("impl");
    }

    std::optional<fir::ConstantValueR> toConstantValue(fir::Context ctx,
                                                       fir::TypeR t);

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
                                                     fir::Context &ctx);

    bool storeConstant(u8 *v, fir::TypeR c) {
      auto bitwidth = c->get_bitwidth();
      if (c->is_float() && bitwidth == 32) {
        auto val = (f32)vals[0].f;
        std::memcpy(v, &val, sizeof(f32));
        return true;
      }
      if (c->is_float() && bitwidth == 64) {
        auto val = (f64)vals[0].f;
        std::memcpy(v, &val, sizeof(f64));
        return true;
      }

      if (c->is_int() && bitwidth == 8) {
        *((i8 *)v) = (i8)vals[0].i;
        return true;
      }
      if (c->is_int() && bitwidth == 32) {
        auto val = (i32)vals[0].i;
        std::memcpy(v, &val, sizeof(i32));
        return true;
      }
      if ((c->is_ptr() || c->is_int()) && bitwidth == 64) {
        auto val = (i64)vals[0].i;
        std::memcpy(v, &val, sizeof(i64));
        return true;
      }
      if (c->is_vec()) {
        auto cv = c->as_vec();
        for (size_t i = 0; i < vals.size(); i++) {
          if (cv.type == fir::VectorType::SubType::Floating &&
              cv.bitwidth == 32) {
            auto val = (f32)vals[0].f;
            std::memcpy((v + (i * cv.bitwidth / 8)), &val, sizeof(f32));
            // *(((f32 *)(v + (i * cv.bitwidth / 8)))) = (f32)vals[i].f;
          } else if (cv.type == fir::VectorType::SubType::Integer &&
                     cv.bitwidth == 32) {
            auto val = (i32)vals[0].i;
            std::memcpy((v + (i * cv.bitwidth / 8)), &val, sizeof(i32));
            // *(((i32 *)(v + (i * cv.bitwidth / 8)))) = (i32)vals[i].i;
          } else if (cv.type == fir::VectorType::SubType::Integer &&
                     cv.bitwidth == 64) {
            auto val = (i64)vals[0].i;
            std::memcpy((v + (i * cv.bitwidth / 8)), &val, sizeof(i64));
            // *(((u64 *)(v + (i * cv.bitwidth / 8)))) = (i64)vals[i].i;
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

    bool operator==(const ConstantValue &other) const;
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

  ConstantValue eval_binary_instr(fir::Context &ctx, fir::Instr instr);
  ConstantValue eval_instr(fir::Context &ctx, fir::Instr instr);

  void eval_and_update(fir::Context &ctx, fir::ValueR value);

  void dump();

  void eval_meets(fir::BasicBlock bb, size_t bb_id);

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

  void apply(fir::Context &ctx, fir::Function &func) override;
};
}  // namespace foptim::optim
