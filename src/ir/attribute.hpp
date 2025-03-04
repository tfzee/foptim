#pragma once
#include "constant_value.hpp"
#include "ir/types_ref.hpp"
#include "utils/logging.hpp"
#include "utils/string.hpp"

namespace foptim::fir {

enum class AttributeType : u8 {
  Void = 0,
  Constant,
  Type,
  String,
};

class Attribute {
public:
  union {
    AttributeType ty;
    struct {
      AttributeType ty;
      ConstantValueR v;
    } const_u;
    struct {
      AttributeType ty;
      TypeR v;
    } type_u;
    struct {
      AttributeType ty;
      IRStringRef v;
    } str_u;
  };

  Attribute() : ty(AttributeType::Void) {}
  Attribute(IRStringRef v) : str_u({AttributeType::String, v}) {}
  Attribute(ConstantValueR v) : const_u({AttributeType::Constant, v}) {}
  Attribute(TypeR v) : type_u({AttributeType::Type, v}) {}

  // ~Attribute() {
  //   switch (ty) {
  //   case AttributeType::Void:
  //   case AttributeType::Constant:
  //   case AttributeType::Type:
  //   case AttributeType::String:
  //     return;
  //   }
  // }

  // Attribute(const Attribute &other) {
  //   switch (other.ty) {
  //   case AttributeType::Void:
  //     ty = other.ty;
  //     return;
  //   case AttributeType::Constant:
  //     const_u = other.const_u;
  //     return;
  //   case AttributeType::Type:
  //     type_u = other.type_u;
  //     return;
  //   case AttributeType::String:
  //     str_u = other.str_u;
  //     return;
  //   }
  // }

  [[nodiscard]] const void *try_void() const {
    if (ty == AttributeType::Void) {
      return (void *)this;
    }
    return nullptr;
  }
  [[nodiscard]] const ConstantValueR *try_constant() const {
    if (ty == AttributeType::Constant) {
      return &const_u.v;
    }
    return nullptr;
  }
  [[nodiscard]] IRStringRef try_string() const {
    if (ty == AttributeType::Constant) {
      return str_u.v;
    }
    return nullptr;
  }
  [[nodiscard]] const TypeR *try_type() const {
    if (ty == AttributeType::Type) {
      return &type_u.v;
    }
    return nullptr;
  }
  [[nodiscard]] TypeR *try_type() {
    if (ty == AttributeType::Type) {
      return &type_u.v;
    }
    return nullptr;
  }
};

} // namespace foptim::fir

inline fmt::appender fmt::formatter<foptim::fir::Attribute>::format(
    foptim::fir::Attribute const &attrib, format_context &ctx) const {
  switch (attrib.ty) {
  case foptim::fir::AttributeType::Void:
    return fmt::format_to(ctx.out(), ": Void");
  case foptim::fir::AttributeType::Constant:
    return fmt::format_to(ctx.out(), ": TODO CONSTANT PRINTING");
  case foptim::fir::AttributeType::Type:
    return fmt::format_to(ctx.out(), ": {}", attrib.type_u.v);
  case foptim::fir::AttributeType::String:
    return fmt::format_to(ctx.out(), ": {}", attrib.str_u.v);
  }
};
