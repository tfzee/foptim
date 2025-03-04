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
  if (ty != other.ty) {
    return false;
  }
  switch (ty) {
  case ConstantType::PoisonValue:
    return true;
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
    return;
  case ConstantType::VectorValue:
    vec_u.v.members.~vector();
    return;
  }
}

ConstantValue &ConstantValue::operator=(const ConstantValue &old) {
  type = old.type;
  switch (old.ty) {
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
  }
}

ConstantValue::ConstantValue(const ConstantValue &old) : type(old.type) {
  switch (old.ty) {
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
  }
}

[[nodiscard]] bool ConstantValue::is_valid() const {
  if (!type.is_valid()) {
    fmt::println("Invalid type\n");
    return false;
  }
  return true;
}

} // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::ConstantValueR>::format(
    foptim::fir::ConstantValueR const &v, format_context &ctx) const {
  switch (v->ty) {
  case foptim::fir::ConstantType::PoisonValue:
    return fmt::format_to(ctx.out(), fg(fmt::color::orange), "POISON");
  case foptim::fir::ConstantType::IntValue:
    return fmt::format_to(ctx.out(), fg(fmt::color::orange), "{}:{}",
                          v->int_u.v.data, v->type);
  case foptim::fir::ConstantType::FloatValue:
    return fmt::format_to(ctx.out(), fg(fmt::color::orange), "{}:{}",
                          v->float_u.v.data, v->type);
  case foptim::fir::ConstantType::GlobalPtr:
    return fmt::format_to(ctx.out(), fg(fmt::color::orange), "G({})",
                          v->gp_u.v.glob->name.c_str());
  case foptim::fir::ConstantType::FuncPtr:
    return fmt::format_to(ctx.out(), fg(fmt::color::orange), "{}",
                          v->fup_u.v.func->getName().c_str());
  case foptim::fir::ConstantType::VectorValue:
    return fmt::format_to(ctx.out(), fg(fmt::color::orange), "VECTOR");
  }
}
