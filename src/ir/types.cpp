#include "ir/types.hpp"

namespace foptim::fir {
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
  case AnyTypeType::Vector:
    return vec_u.v.get_size();
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
  case AnyTypeType::Vector:
    return vec_u.v.eql(other.vec_u.v);
  }
}
AnyType &AnyType::operator=(const AnyType &old) {
  if (this == &old) {
    return *this;
  }

  switch (old.ty) {
  case AnyTypeType::Ptr:
  case AnyTypeType::Void:
    ty = old.ty;
    return *this;
  case AnyTypeType::Integer:
    int_u = old.int_u;
    return *this;
  case AnyTypeType::Float:
    float_u = old.float_u;
    return *this;
  case AnyTypeType::Function:
    func_u = {old.ty, old.func_u.v};
    func_u.v.arg_types = old.func_u.v.arg_types;
    return *this;
  case AnyTypeType::Vector:
    vec_u = old.vec_u;
    return *this;
  }
}

AnyType::AnyType(const AnyType &old) {
  switch (old.ty) {
  case AnyTypeType::Void:
  case AnyTypeType::Ptr:
    ty = old.ty;
    return;
  case AnyTypeType::Integer:
    int_u = old.int_u;
    return;
  case AnyTypeType::Float:
    float_u = old.float_u;
    return;
  case AnyTypeType::Function:
    func_u = {old.ty, old.func_u.v};
    func_u.v.arg_types = old.func_u.v.arg_types;
    return;
  case AnyTypeType::Vector:
    vec_u = old.vec_u;
    return;
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
  }
}

} // namespace foptim::fir

fmt::appender
fmt::formatter<foptim::fir::TypeR>::format(foptim::fir::TypeR const &v,
                                           format_context &ctx) const {
  constexpr auto col = fg(fmt::color::light_coral);
  if (!v.is_valid()) {
    return fmt::format_to(ctx.out(), col, "INVALID");
  }
  switch (v->ty) {
  case foptim::fir::AnyTypeType::Void:
    return fmt::format_to(ctx.out(), col, "()");
  case foptim::fir::AnyTypeType::Integer:
    return fmt::format_to(ctx.out(), col, "i{}", v->as_int());
  case foptim::fir::AnyTypeType::Float:
    return fmt::format_to(ctx.out(), col, "f{}", v->as_float());
  case foptim::fir::AnyTypeType::Ptr:
    return fmt::format_to(ctx.out(), col, "ptr");
  case foptim::fir::AnyTypeType::Function:
    return fmt::format_to(ctx.out(), col, "FUNC");
  case foptim::fir::AnyTypeType::Vector:
    return fmt::format_to(ctx.out(), col, "VEC");
  }
}
