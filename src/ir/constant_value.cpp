#include "constant_value.hpp"

#include <fmt/color.h>

#include "function.hpp"
#include "ir/constant_value_ref.hpp"
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

bool ConstantStruct::operator==(const ConstantStruct &other) const {
  if (v.size() != other.v.size()) {
    return false;
  }
  for (size_t i = 0; i < v.size(); i++) {
    if (!v[i].eql(other.v[i])) {
      return false;
    }
  }
  return true;
}

void ConstantValue::add_usage(Use u) {
  switch (ty) {
    case ConstantType::NullPtr:
    case ConstantType::PoisonValue:
    case ConstantType::IntValue:
    case ConstantType::FloatValue:
    case ConstantType::VectorValue:
    case ConstantType::ConstantStruct:
      return;
    case ConstantType::GlobalPtr:
      return gp_u.v.glob->add_usage(u);
    case ConstantType::FuncPtr:
      return fup_u.v.func->add_usage(u);
  }
}
[[nodiscard]] size_t ConstantValue::get_n_uses() const {
  switch (ty) {
    case ConstantType::NullPtr:
    case ConstantType::PoisonValue:
    case ConstantType::IntValue:
    case ConstantType::FloatValue:
    case ConstantType::VectorValue:
    case ConstantType::ConstantStruct:
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
    case ConstantType::ConstantStruct:
      return;
    case ConstantType::GlobalPtr:
      return gp_u.v.glob->remove_usage(u, verify);
    case ConstantType::FuncPtr:
      return fup_u.v.func->remove_usage(u, verify);
  }
}
void ConstantValue::replace_all_uses(ValueR v) {
  switch (ty) {
    case ConstantType::NullPtr:
    case ConstantType::PoisonValue:
    case ConstantType::IntValue:
    case ConstantType::FloatValue:
    case ConstantType::VectorValue:
    case ConstantType::ConstantStruct:
      return;
    case ConstantType::GlobalPtr:
      return gp_u.v.glob->replace_all_uses(v);
    case ConstantType::FuncPtr:
      return fup_u.v.func->replace_all_uses(v);
  }
}
[[nodiscard]] IRVec<Use> *ConstantValue::get_uses() {
  switch (ty) {
    case ConstantType::NullPtr:
    case ConstantType::PoisonValue:
    case ConstantType::IntValue:
    case ConstantType::FloatValue:
    case ConstantType::VectorValue:
    case ConstantType::ConstantStruct:
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
    case ConstantType::ConstantStruct:
      return nullptr;
    case ConstantType::GlobalPtr:
      return &gp_u.v.glob->get_uses();
    case ConstantType::FuncPtr:
      return &fup_u.v.func->get_uses();
  }
}

TypeR ConstantValue::get_type() const { return type; }

bool ConstantValue::eql(const ConstantValue &other) const {
  if (ty != other.ty) {
    return false;
  }
  switch (ty) {
    case ConstantType::NullPtr:
    case ConstantType::PoisonValue:
      return true;
    case ConstantType::ConstantStruct:
      return stru_u.v == other.stru_u.v;
    case ConstantType::IntValue:
      return int_u.v == other.int_u.v;
    case ConstantType::FloatValue:
      return float_u.v == other.float_u.v;
    case ConstantType::VectorValue:
      return vec_u.v == other.vec_u.v;
    case ConstantType::GlobalPtr:
      return gp_u.v == other.gp_u.v;
    case ConstantType::FuncPtr:
      return fup_u.v == other.fup_u.v;
  }
}

ConstantValue::~ConstantValue() {
  switch (ty) {
    case ConstantType::PoisonValue:
    case ConstantType::IntValue:
    case ConstantType::FloatValue:
    case ConstantType::GlobalPtr:
    case ConstantType::FuncPtr:
    case ConstantType::NullPtr:
      return;
    case ConstantType::ConstantStruct:
      stru_u.v.v.~vector();
      return;
    case ConstantType::VectorValue:
      vec_u.v.members.~vector();
      return;
  }
}

ConstantValue &ConstantValue::operator=(const ConstantValue &old) {
  type = old.type;
  switch (old.ty) {
    case ConstantType::NullPtr:
    case ConstantType::PoisonValue:
      ty = old.ty;
      return *this;
    case ConstantType::IntValue:
      int_u = old.int_u;
      return *this;
    case ConstantType::FloatValue:
      float_u = old.float_u;
      return *this;
    case ConstantType::GlobalPtr:
      gp_u = old.gp_u;
      return *this;
    case ConstantType::FuncPtr:
      fup_u = old.fup_u;
      return *this;
    case ConstantType::VectorValue:
      vec_u = {old.ty, old.vec_u.v};
      return *this;
    case ConstantType::ConstantStruct:
      stru_u = {old.ty, old.stru_u.v};
      return *this;
  }
}

ConstantValue::ConstantValue(const ConstantValue &old) : type(old.type) {
  switch (old.ty) {
    case ConstantType::NullPtr:
    case ConstantType::PoisonValue:
      ty = old.ty;
      return;
    case ConstantType::IntValue:
      int_u = old.int_u;
      return;
    case ConstantType::FloatValue:
      float_u = old.float_u;
      return;
    case ConstantType::GlobalPtr:
      gp_u = old.gp_u;
      return;
    case ConstantType::FuncPtr:
      fup_u = old.fup_u;
      return;
    case ConstantType::VectorValue:
      vec_u = {old.ty, old.vec_u.v};
      return;
    case ConstantType::ConstantStruct:
      stru_u = {old.ty, old.stru_u.v};
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
  if (type->is_struct() && ty != ConstantType::PoisonValue &&
      ty != ConstantType::ConstantStruct) {
    fmt::println("Type is struct but constant is not\n");
    return false;
  }
  return true;
}

}  // namespace foptim::fir

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
        return fmt::format_to(ctx.out(), colnumber, "{}:{:c}", v.int_u.v.data,
                              v.type);
      } else {
        return fmt::format_to(ctx.out(), "{}:{}", v.int_u.v.data, v.type);
      }
    case foptim::fir::ConstantType::FloatValue:
      if (debug) {
        if (v.type->as_float() == 32) {
          return fmt::format_to(
              ctx.out(), colnumber, "{:X}:{}",
              (foptim::u32)std::bit_cast<foptim::u64>(v.float_u.v.data),
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
        return fmt::format_to(ctx.out(), colnumber, "{}:{}", v.as_f64(),
                              v.type);
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
    case foptim::fir::ConstantType::ConstantStruct:
      return fmt::format_to(ctx.out(), colconst, "STRUCT");
  }
}
