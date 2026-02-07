#include "ir/types.hpp"

#include <fmt/color.h>

namespace foptim::fir {

[[nodiscard]] bool FunctionType::eql(const FunctionType &other) const {
  if (!return_type->eql(*other.return_type.get_raw_ptr()) ||
      arg_types.size() != other.arg_types.size()) {
    return false;
  }
  fori(arg_types) {
    if (!arg_types[i]->eql(*other.arg_types[i].get_raw_ptr())) {
      return false;
    }
  }
  return true;
}

u32 AnyType::get_size() const {
  switch (ty) {
    case AnyTypeType::Void:
      return 0;
    case AnyTypeType::Ptr:
      return 8;
    case AnyTypeType::Integer:
      return int_u.v.get_size();
    case AnyTypeType::Float:
      return float_u.v.get_size();
    case AnyTypeType::Function:
      return func_u.v.get_size();
    case AnyTypeType::Struct:
      return struct_u.v.get_size();
    case AnyTypeType::Vector:
      return vec_u.v.get_size();
  }
}

u32 AnyType::get_bitwidth() const {
  switch (ty) {
    case AnyTypeType::Void:
      return 0;
    case AnyTypeType::Ptr:
      return 8 * 8;
    case AnyTypeType::Integer:
      return int_u.v.bitwidth;
    case AnyTypeType::Float:
      return float_u.v.bitwidth;
    case AnyTypeType::Function:
      return func_u.v.get_bitwidth();
    case AnyTypeType::Struct:
      return struct_u.v.get_bitwidth();
    case AnyTypeType::Vector:
      return vec_u.v.bitwidth * vec_u.v.member_number;
  }
}

u32 AnyType::get_align() const {
  switch (ty) {
    case AnyTypeType::Void:
      return 0;
    case AnyTypeType::Ptr:
      return 8;
    case AnyTypeType::Integer:
      return int_u.v.get_align();
    case AnyTypeType::Float:
      return float_u.v.get_align();
    case AnyTypeType::Function:
      return func_u.v.get_align();
    case AnyTypeType::Struct:
      return struct_u.v.get_align();
    case AnyTypeType::Vector:
      return vec_u.v.get_align();
  }
}

[[nodiscard]] bool AnyType::eql(const AnyType &other) const {
  if (ty != other.ty) {
    return false;
  }
  switch (ty) {
    case AnyTypeType::Void:
    case AnyTypeType::Ptr:
      return true;
    case AnyTypeType::Integer:
      return int_u.v.eql(other.int_u.v);
    case AnyTypeType::Float:
      return float_u.v.eql(other.float_u.v);
    case AnyTypeType::Function:
      return func_u.v.eql(other.func_u.v);
    case AnyTypeType::Struct:
      return struct_u.v.eql(other.struct_u.v);
    case AnyTypeType::Vector:
      return vec_u.v.eql(other.vec_u.v);
  }
}

AnyType &AnyType::operator=(const AnyType &old) {
  if (this == &old) return *this;
  this->~AnyType();
  this->ty = old.ty;

  switch (old.ty) {
    case AnyTypeType::Ptr:
    case AnyTypeType::Void:
      return *this;
    case AnyTypeType::Integer:
      int_u = old.int_u;
      return *this;
    case AnyTypeType::Float:
      float_u = old.float_u;
      return *this;
    case AnyTypeType::Vector:
      vec_u = old.vec_u;
      return *this;
    case AnyTypeType::Function:
      new (&func_u.v) auto(old.func_u.v);
      return *this;
    case AnyTypeType::Struct:
      new (&struct_u.v) auto(old.struct_u.v);
      return *this;
    default:
      return *this;
  }
}

AnyType::AnyType(const AnyType &old) : ty(old.ty) {
  switch (old.ty) {
    case AnyTypeType::Void:
    case AnyTypeType::Ptr:
      return;
    case AnyTypeType::Integer:
      int_u = old.int_u;
      break;
    case AnyTypeType::Float:
      float_u = old.float_u;
      break;
    case AnyTypeType::Vector:
      vec_u = old.vec_u;
      break;
    case AnyTypeType::Function:
      new (&func_u.v) auto(old.func_u.v);
      break;
    case AnyTypeType::Struct:
      new (&struct_u.v) auto(old.struct_u.v);
      break;
  }
}

AnyType::~AnyType() {
  switch (ty) {
    case AnyTypeType::Void:
    case AnyTypeType::Integer:
    case AnyTypeType::Float:
    case AnyTypeType::Ptr:
    case AnyTypeType::Vector:
      return;
    case AnyTypeType::Function:
      func_u.v.arg_types.~vector();
      return;
    case AnyTypeType::Struct:
      struct_u.v.elems.~vector();
      return;
  }
}

u32 StructType::get_align() const {
  u32 align = 0;
  for (auto [_, elem] : elems) {
    align = std::max(elem->get_align(), align);
  }
  return align;
}

u32 StructType::get_size() const {
  if (elems.size() > 0) {
    auto [off, ty] = elems.back();
    auto size = off + ty->get_size();
    auto align_off = size % get_align();
    return size + align_off;
  }
  return 0;
}

u32 StructType::get_bitwidth() const {
  if (elems.size() > 0) {
    auto [off, ty] = elems.back();
    auto size = off + ty->get_bitwidth();
    auto align_off = (ty->get_size() % get_align()) * 8;
    return size + align_off;
  }
  return 0;
}

}  // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::AnyType>::format(
    foptim::fir::AnyType const &v, format_context &ctx) const {
  auto app = ctx.out();
  const auto col = color ? color_type : text_style{};
  switch (v.ty) {
    case foptim::fir::AnyTypeType::Void:
      return fmt::format_to(app, col, "()");
    case foptim::fir::AnyTypeType::Integer:
      return fmt::format_to(app, col, "i{}", v.as_int());
    case foptim::fir::AnyTypeType::Float:
      return fmt::format_to(app, col, "f{}", v.as_float());
    case foptim::fir::AnyTypeType::Ptr:
      return fmt::format_to(app, col, "ptr");
    case foptim::fir::AnyTypeType::Function: {
      auto func = v.as_func();
      app = fmt::format_to(app, col, "{}(", func.return_type);
      for (auto arg : func.arg_types) {
        app = fmt::format_to(app, col, "{} ", arg);
      }
      return fmt::format_to(app, col, ")");
    }
    case foptim::fir::AnyTypeType::Vector:
      switch (v.vec_u.v.type) {
        case foptim::fir::VectorType::SubType::Integer:
          return fmt::format_to(app, col, "{}@i{}", v.vec_u.v.member_number,
                                v.vec_u.v.bitwidth);
          break;
        case foptim::fir::VectorType::SubType::Floating:
          return fmt::format_to(app, col, "{}@f{}", v.vec_u.v.member_number,
                                v.vec_u.v.bitwidth);
          break;
      }
    case foptim::fir::AnyTypeType::Struct:
      app = fmt::format_to(app, col, "{{");
      for (auto member : v.as_struct().elems) {
        app = fmt::format_to(app, col, "{} ", member.ty);
      }
      return fmt::format_to(app, col, "}}");
  }
}
