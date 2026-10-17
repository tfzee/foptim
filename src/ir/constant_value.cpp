#include "constant_value.hpp"

#include <fmt/color.h>

#include "function.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/context.hpp"
#include "ir/types.hpp"
#include "ir/value.hpp"
#include "utils/logging.hpp"

namespace foptim::fir {

bool VectorValue::operator==(const VectorValue &other) const {
  if (members.size() != other.members.size()) {
    return false;
  }
  for (size_t i = 0; i < members.size(); i++) {
    if (!members[i]->eql(*other.members[i].get_raw_ptr())) {
      return false;
    }
  }
  return true;
}

ConstantValue::ConstantValue(i128 v, TypeR typee)
    : ConstantValue(APInt::from_i128(typee->get_bitwidth(), v), typee) {}

ConstantValue::ConstantValue(i64 v, TypeR typee)
    : ConstantValue(APInt::from_i64(typee->get_bitwidth(), v), typee) {}

ConstantValue::ConstantValue(u64 v, TypeR typee)
    : ConstantValue(APInt::from_u64(typee->get_bitwidth(), v), typee) {}

u64 ConstantValue::hash() const {
  u64 h = hash_mix(static_cast<u64>(ty),
                   reinterpret_cast<uintptr_t>(type.data_ref));
  switch (ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
    return h;
  case ConstantType::IntValue: {
    return hash_mix(hash_mix(h, int_u.v.data.bits()), int_u.v.data.hash());
  }
  case ConstantType::FloatValue:
    return hash_mix(h, std::bit_cast<u64>(float_u.v.data));
  case ConstantType::VectorValue:
    for (const auto &m : vec_u.v.members) {
      h = hash_mix(h, m->hash());
    }
    return h;
  case ConstantType::GlobalPtr:
    return hash_mix(h, reinterpret_cast<uintptr_t>(gp_u.v.glob.data_ref));
  case ConstantType::FuncPtr:
    return hash_mix(h, reinterpret_cast<uintptr_t>(fup_u.v.func.func));
  }
}

void ConstantValue::add_usage(Use u) {
  switch (ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
  case ConstantType::IntValue:
  case ConstantType::FloatValue:
  case ConstantType::VectorValue:
    return;
  case ConstantType::GlobalPtr:
    gp_u.v.glob->add_usage(u);
    return;
  case ConstantType::FuncPtr:
    fup_u.v.func->add_usage(u);
    return;
  }
}
[[nodiscard]] size_t ConstantValue::get_n_uses() const {
  switch (ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
  case ConstantType::IntValue:
  case ConstantType::FloatValue:
  case ConstantType::VectorValue:
    return 0;
  case ConstantType::GlobalPtr:
    return gp_u.v.glob->get_n_uses();
  case ConstantType::FuncPtr:
    return fup_u.v.func->get_n_uses();
  }
}
void ConstantValue::remove_usage(Use u, bool verify) {
  switch (ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
  case ConstantType::IntValue:
  case ConstantType::FloatValue:
  case ConstantType::VectorValue:
    return;
  case ConstantType::GlobalPtr:
    gp_u.v.glob->remove_usage(u, verify);
    return;
  case ConstantType::FuncPtr:
    fup_u.v.func->remove_usage(u, verify);
    return;
  }
}
void ConstantValue::replace_uses_outside_block(fir::BasicBlock bb,
                                               ValueR new_value) {
  switch (ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
  case ConstantType::IntValue:
  case ConstantType::FloatValue:
  case ConstantType::VectorValue:
    return;
  case ConstantType::GlobalPtr:
    gp_u.v.glob->replace_uses_outside_block(bb, new_value);
    return;
  case ConstantType::FuncPtr:
    fup_u.v.func->replace_uses_outside_block(bb, new_value);
    return;
  }
}
void ConstantValue::replace_all_uses(ValueR v) {
  switch (ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
  case ConstantType::IntValue:
  case ConstantType::FloatValue:
  case ConstantType::VectorValue:
    return;
  case ConstantType::GlobalPtr:
    gp_u.v.glob->replace_all_uses(v);
    return;
  case ConstantType::FuncPtr:
    fup_u.v.func->replace_all_uses(v);
    return;
  }
}
[[nodiscard]] IRVec<Use> *ConstantValue::get_uses() {
  switch (ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
  case ConstantType::IntValue:
  case ConstantType::FloatValue:
  case ConstantType::VectorValue:
    return nullptr;
  case ConstantType::GlobalPtr:
    // return gp_u.v.glob->get_uses();
  case ConstantType::FuncPtr:
    TODO("idk if this is legal");
    // return &fup_u.v.func->get_uses();
  }
}
[[nodiscard]] const IRVec<Use> *ConstantValue::get_uses() const {
  switch (ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
  case ConstantType::IntValue:
  case ConstantType::FloatValue:
  case ConstantType::VectorValue:
    return nullptr;
  case ConstantType::GlobalPtr:
    return &gp_u.v.glob->get_uses();
  case ConstantType::FuncPtr:
    return &fup_u.v.func->get_uses();
  }
}

ConstantValue::~ConstantValue() {
  switch (ty) {
  case ConstantType::PoisonValue:
  case ConstantType::FloatValue:
  case ConstantType::GlobalPtr:
  case ConstantType::FuncPtr:
  case ConstantType::NullPtr:
    return;
  case ConstantType::IntValue:
    int_u.v.~IntValue();
    return;
  case ConstantType::VectorValue:
    vec_u.v.members.~vector();
    return;
  }
}

ConstantValue &ConstantValue::operator=(const ConstantValue &old) {
  if (this == &old) {
    return *this;
  }
  this->~ConstantValue();
  type = old.type;
  switch (old.ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
    ty = old.ty;
    return *this;
  case ConstantType::IntValue:
    new (&int_u.v) IntValue(old.int_u.v);
    int_u._ty = old.int_u._ty;
    return *this;
  case ConstantType::FloatValue:
    float_u = old.float_u;
    float_u._ty = old.float_u._ty;
    return *this;
  case ConstantType::GlobalPtr:
    gp_u = old.gp_u;
    gp_u._ty = old.gp_u._ty;
    return *this;
  case ConstantType::FuncPtr:
    fup_u = old.fup_u;
    fup_u._ty = old.fup_u._ty;
    return *this;
  case ConstantType::VectorValue:
    new (&vec_u.v) VectorValue(old.vec_u.v);
    vec_u._ty = old.ty;
    // vec_u = {._ty = old.ty, .v = old.vec_u.v};
    return *this;
  }
}

ConstantValue::ConstantValue(const ConstantValue &old) : type(old.type) {
  type = old.type;
  switch (old.ty) {
  case ConstantType::NullPtr:
  case ConstantType::PoisonValue:
    ty = old.ty;
    return;
  case ConstantType::IntValue:
    new (&int_u.v) IntValue(old.int_u.v);
    int_u._ty = old.int_u._ty;
    return;
  case ConstantType::FloatValue:
    float_u = old.float_u;
    float_u._ty = old.float_u._ty;
    return;
  case ConstantType::GlobalPtr:
    gp_u = old.gp_u;
    gp_u._ty = old.gp_u._ty;
    return;
  case ConstantType::FuncPtr:
    fup_u = old.fup_u;
    fup_u._ty = old.fup_u._ty;
    return;
  case ConstantType::VectorValue:
    new (&vec_u.v) VectorValue(old.vec_u.v);
    vec_u._ty = old.ty;
    // vec_u = {old.ty, old.vec_u.v};
    return;
  }
}

[[nodiscard]] bool ConstantValue::is_valid() const {
  if (!type.is_valid()) {
    fmt::println("Invalid type\n");
    return false;
  }
  if (type->is_int() &&
      (ty != ConstantType::PoisonValue && ty != ConstantType::IntValue)) {
    fmt::println("Type is int but constant is not\n");
    return false;
  }
  if (type->is_float() &&
      (ty != ConstantType::PoisonValue && ty != ConstantType::FloatValue)) {
    fmt::println("Type is float but constant is not\n");
    return false;
  }
  if (type->is_func() &&
      (ty != ConstantType::PoisonValue && ty != ConstantType::GlobalPtr &&
       ty != ConstantType::FuncPtr)) {
    fmt::println("Type is func but constant is not\n");
    return false;
  }
  if (type->is_ptr() &&
      (ty != ConstantType::PoisonValue && ty != ConstantType::GlobalPtr &&
       ty != ConstantType::FuncPtr && ty != ConstantType::IntValue &&
       ty != ConstantType::NullPtr)) {
    fmt::println("Type is ptr but constant is not\n");
    return false;
  }
  return true;
}
std::optional<fir::ConstantValueR>
ConstantValue::bit_cast(Context &ctx, fir::TypeR target_type) {
  auto old_ty = get_type();
  if (old_ty->get_bitwidth() != target_type->get_bitwidth()) {
    return {};
  }

  if (old_ty->is_float() && (target_type->is_int() || target_type->is_ptr())) {
    if (old_ty->get_bitwidth() == 32) {
      return {
          ctx->get_constant_value(std::bit_cast<u32>(as_f32()), target_type)};
    }
    if (old_ty->get_bitwidth() == 64) {
      return {
          ctx->get_constant_value(std::bit_cast<u64>(as_f64()), target_type)};
    }
  }
  if ((old_ty->is_int() || old_ty->is_ptr()) && target_type->is_float()) {
    if (old_ty->get_bitwidth() == 32) {
      return {ctx->get_constant_value(
          std::bit_cast<f32>(static_cast<u32>(as_apint().to_u64())),
          target_type)};
    }
    if (old_ty->get_bitwidth() == 64) {
      return {ctx->get_constant_value(
          std::bit_cast<f64>(as_apint().to_u64()),
          target_type)};
    }
  }
  if (!old_ty->is_vec() && target_type->is_vec()) {
    auto target = target_type->as_vec();
    const u32 total = old_ty->get_bitwidth();
    APInt value = APInt::zero(total);
    if (old_ty->is_int() || old_ty->is_ptr()) {
      value = is_null() ? APInt::zero(total) : as_apint();
    } else if (old_ty->is_f32()) {
      value = APInt(total, u64{std::bit_cast<u32>(as_f32())});
    } else if (old_ty->is_f64()) {
      value = APInt(total, std::bit_cast<u64>(as_f64()));
    } else {
      TODO("UNREACH?");
    }
    IRVec<ConstantValueR> values;

    for (u32 i = 0; i < target.member_number; i++) {
      const APInt curr_val =
          value.lshr(i * target.bitwidth).trunc(target.bitwidth);
      if (target.type == VectorType::SubType::Integer) {
        values.push_back(ctx->get_constant_int(curr_val));
      } else if (target.type == VectorType::SubType::Floating &&
                 target.bitwidth == 32) {
        values.push_back(ctx->get_constant_value(
            std::bit_cast<f32>(static_cast<u32>(curr_val.to_u64())),
            ctx->get_float_type(target.bitwidth)));
      } else if (target.type == VectorType::SubType::Floating &&
                 target.bitwidth == 64) {
        values.push_back(ctx->get_constant_value(
            std::bit_cast<f64>(curr_val.to_u64()),
            ctx->get_float_type(target.bitwidth)));
      }
    }

    return {ctx->get_constant_value(std::move(values), target_type)};
  }

  // integer vector to integer vector with other lane width (little endian)
  if (is_vec() && old_ty->is_vec() && target_type->is_vec() &&
      old_ty->get_bitwidth() <= 512) {
    const auto src = old_ty->as_vec();
    const auto target = target_type->as_vec();
    auto lane_ok = [](const VectorType &v) {
      return v.type == VectorType::SubType::Integer ||
             (v.type == VectorType::SubType::Floating &&
              (v.bitwidth == 32 || v.bitwidth == 64));
    };
    bool all_int = lane_ok(src) && lane_ok(target) && src.bitwidth % 8 == 0 &&
                   target.bitwidth % 8 == 0 && target.bitwidth <= 64;
    for (auto m : as_vec().members) {
      all_int = all_int && (m->is_int() || m->is_float());
    }
    if (all_int) {
      u8 bytes[64] = {};
      const u32 src_bytes = src.bitwidth / 8;
      for (size_t i = 0; i < as_vec().members.size(); i++) {
        auto m = as_vec().members[i];
        APInt lane = APInt::zero(src.bitwidth);
        if (m->is_float() && src.bitwidth == 32) {
          lane = APInt(src.bitwidth, u64{std::bit_cast<u32>(m->as_f32())});
        } else if (m->is_float()) {
          lane = APInt(src.bitwidth, std::bit_cast<u64>(m->as_f64()));
        } else {
          lane = m->as_apint();
        }
        for (u32 j = 0; j < src_bytes; j++) {
          bytes[i * src_bytes + j] = static_cast<u8>(lane.lshr(8 * j).to_u64());
        }
      }
      IRVec<ConstantValueR> values;
      const u32 target_bytes = target.bitwidth / 8;
      for (u32 i = 0; i < target.member_number; i++) {
        u64 lane = 0;
        for (u32 j = 0; j < target_bytes; j++) {
          lane |= static_cast<u64>(bytes[i * target_bytes + j]) << (8 * j);
        }
        if (target.type == VectorType::SubType::Floating) {
          auto ft = ctx->get_float_type(target.bitwidth);
          if (target.bitwidth == 32) {
            values.push_back(ctx->get_constant_value(
                std::bit_cast<f32>(static_cast<u32>(lane)), ft));
          } else {
            values.push_back(
                ctx->get_constant_value(std::bit_cast<f64>(lane), ft));
          }
        } else {
          values.push_back(ctx->get_constant_int(APInt(target.bitwidth, lane)));
        }
      }
      return {ctx->get_constant_value(std::move(values), target_type)};
    }
  }

  (void)ctx;
  fmt::println("{:cd}", *this);
  fmt::println("{:cd}", target_type);
  TODO("impl");
}

} // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::ConstantValueR>::format(
    foptim::fir::ConstantValueR const &v, format_context &ctx) const {
  if (color && debug) {
    return fmt::format_to(ctx.out(), "{:cd}", *v.get_raw_ptr());
  }
  if (color) {
    return fmt::format_to(ctx.out(), "{:c}", *v.get_raw_ptr());
  }
  if (debug) {
    return fmt::format_to(ctx.out(), "{:d}", *v.get_raw_ptr());
  }
  return fmt::format_to(ctx.out(), "{}", *v.get_raw_ptr());
}

fmt::appender fmt::formatter<foptim::fir::ConstantValue>::format(
    foptim::fir::ConstantValue const &v, format_context &ctx) const {
  auto colconst = color ? color_constant : text_style{};
  auto colnumber = color ? color_number : text_style{};
  auto colfunc = color ? color_func : text_style{};
  switch (v.ty) {
  case foptim::fir::ConstantType::NullPtr:
    return fmt::format_to(ctx.out(), colconst, "NULL");
  case foptim::fir::ConstantType::PoisonValue:
    return fmt::format_to(ctx.out(), colconst, "POISON");
  case foptim::fir::ConstantType::IntValue:
    if (color) {
      return fmt::format_to(ctx.out(), colnumber, "{}:{:c}", v.as_int(),
                            v.type);
    } else {
      return fmt::format_to(ctx.out(), "{}:{}", v.as_int(), v.type);
    }
  case foptim::fir::ConstantType::FloatValue:
    if (debug) {
      if (v.type->as_float() == 32) {
        return fmt::format_to(ctx.out(), colnumber, "{:X}:{}",
                              static_cast<foptim::u32>(
                                  std::bit_cast<foptim::u64>(v.float_u.v.data)),
                              v.type);
      }
      return fmt::format_to(ctx.out(), colnumber, "{:X}:{}",
                            std::bit_cast<foptim::u64>(v.float_u.v.data),
                            v.type);

    } else {
      if (v.type->as_float() == 32) {
        return fmt::format_to(ctx.out(), colnumber, "{}:{}", v.as_f32(),
                              v.type);
      }
      return fmt::format_to(ctx.out(), colnumber, "{}:{}", v.as_f64(), v.type);
    }
  case foptim::fir::ConstantType::GlobalPtr:
    return fmt::format_to(ctx.out(), colconst, "G({})",
                          v.gp_u.v.glob->name.c_str());
  case foptim::fir::ConstantType::FuncPtr:
    return fmt::format_to(ctx.out(), colfunc, "{}",
                          v.fup_u.v.func->getName().c_str());
  case foptim::fir::ConstantType::VectorValue: {
    const auto &va = v.vec_u.v;
    auto ct = fmt::format_to(ctx.out(), colconst, "<");
    for (auto m : va.members) {
      ct = fmt::format_to(ct, colconst, "{}, ", m);
    }
    return fmt::format_to(ct, colconst, ">:{}", v.type);
  }
  }
}
