#include "constant_value.hpp"

namespace foptim::fir {

bool VectorValue::operator==(const VectorValue &other) const {
  if (members.size() != other.members.size()) {
    return false;
  }
  for (size_t i = 0; i < members.size(); i++) {
    if (members[i].eql(other.members[i])) {
      return false;
    }
  }
  return true;
}
TypeR ConstantValue::get_type() const { return type; }

bool ConstantValue::eql(const ConstantValue &other) const {
  // if(auto* v = std::get_if()
  return std::visit(
      [other](auto &&v) -> bool {
        return std::visit(
            [v](auto &&o) -> bool {
              if constexpr (typeid(o) != typeid(v)) {
                return false;
              } else {
                return v == o;
              }
            },
            other.value);
      },
      value);
}

} // namespace foptim::fir
