#pragma once
#include "constant_value.hpp"
#include "ir/types_ref.hpp"
#include "utils/logging.hpp"
#include "utils/string.hpp"

namespace foptim::fir {

struct VoidAttrib {};

class Attribute {
  using AttribType =
      std::variant<ConstantValue, TypeR, IRString, VoidAttrib>;
  AttribType data;

public:
  Attribute() : data(VoidAttrib{}) {}
  Attribute(IRString v) : data(v) {}
  Attribute(ConstantValue v) : data(v) {}
  Attribute(TypeR v) : data(v) {}

  [[nodiscard]] const auto *try_string() const { return std::get_if<IRString>(&data); }
  [[nodiscard]] const TypeR *try_type() const { return std::get_if<TypeR>(&data); }
  [[nodiscard]] TypeR *try_type() { return std::get_if<TypeR>(&data); }
  [[nodiscard]] const AttribType &get_raw() const { return data; }
};

} // namespace foptim::fir
