#pragma once
#include "constant_value.hpp"
#include "ir/types_ref.hpp"
#include "utils/logging.hpp"
#include "utils/string.hpp"

namespace foptim::fir {

struct VoidAttrib {};

class Attribute {
  using AttribType = std::variant<ConstantValueR, TypeR, IRString, VoidAttrib>;
  AttribType data;

public:
  Attribute() : data(VoidAttrib{}) {}
  Attribute(IRString v) : data(v) {}
  Attribute(ConstantValueR v) : data(v) {}
  Attribute(TypeR v) : data(v) {}

  [[nodiscard]] const auto *try_void() const {
    return std::get_if<VoidAttrib>(&data);
  }
  [[nodiscard]] const auto *try_constant() const {
    return std::get_if<ConstantValueR>(&data);
  }
  [[nodiscard]] const auto *try_string() const {
    return std::get_if<IRString>(&data);
  }
  [[nodiscard]] const TypeR *try_type() const {
    return std::get_if<TypeR>(&data);
  }
  [[nodiscard]] TypeR *try_type() { return std::get_if<TypeR>(&data); }
  [[nodiscard]] const AttribType &get_raw() const { return data; }
};

} // namespace foptim::fir

inline fmt::appender fmt::formatter<foptim::fir::Attribute>::format(
    foptim::fir::Attribute const &attrib, format_context &ctx) const {
  if (const auto *v = attrib.try_constant()) {
    return fmt::format_to(ctx.out(), ": TODO CONSTANT PRINTING");
  }
  if (const auto *v = attrib.try_void()) {
    return fmt::format_to(ctx.out(), ": Void");
  }
  if (const auto *v = attrib.try_string()) {
    return fmt::format_to(ctx.out(), ": {}", v->c_str());
  }
  if (const auto *v = attrib.try_type()) {
    return fmt::format_to(ctx.out(), ": {}", *v);
  }
  return ctx.out();
};
