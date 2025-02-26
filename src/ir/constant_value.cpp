#include "constant_value.hpp"
#include "function.hpp"

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

fmt::appender fmt::formatter<foptim::fir::ConstantValueR>::format(
    foptim::fir::ConstantValueR const &val, format_context &ctx) const {

  if(const auto* v = std::get_if<foptim::fir::IntValue>(&val->value)){
        return fmt::format_to(ctx.out(), fg(fmt::color::orange), "{}:{}", v->data, val->type);
  }
  if(const auto* v = std::get_if<foptim::fir::FloatValue>(&val->value)){
        return fmt::format_to(ctx.out(), fg(fmt::color::orange), "{}:{}", v->data, val->type);
  }
  if(const auto* v = std::get_if<foptim::fir::GlobalPointer>(&val->value)){
        return fmt::format_to(ctx.out(), fg(fmt::color::orange), "G({})", v->glob->name.c_str());
  }
  if(const auto* v = std::get_if<foptim::fir::FunctionPtr>(&val->value)){
        return fmt::format_to(ctx.out(), fg(fmt::color::orange), "{}", v->func->getName().c_str());
  }
  if(const auto* v = std::get_if<foptim::fir::PoisonValue>(&val->value)){
        return fmt::format_to(ctx.out(), fg(fmt::color::orange), "POISON");
  }

  return fmt::format_to(ctx.out(), "constant idk");
}
