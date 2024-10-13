#pragma once
#include "constant_value.hpp"
#include "ir/types_ref.hpp"
#include "utils/logging.hpp"
#include <string>

namespace foptim::fir {

struct VoidAttrib {};

class Attribute {
  using AttribType =
      std::variant<ConstantValue, TypeR, std::string, VoidAttrib>;
  AttribType data;

public:
  Attribute() : data(VoidAttrib{}) {}
  Attribute(std::string v) : data(v) {}
  Attribute(ConstantValue v) : data(v) {}
  Attribute(TypeR v) : data(v) {}

  const auto *try_string() const { return std::get_if<std::string>(&data); }
  const TypeR *try_type() const { return std::get_if<TypeR>(&data); }
  TypeR *try_type() { return std::get_if<TypeR>(&data); }
  const AttribType &get_raw() const { return data; }
};

} // namespace foptim::fir
