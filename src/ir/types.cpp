#include "ir/types.hpp"

#include <fmt/color.h>

#include "ir/intern_table.hpp"

namespace foptim::fir {

[[nodiscard]] bool FunctionType::eql(const FunctionType &other) const {
  if (return_types.size() != other.return_types.size() ||
      arg_types.size() != other.arg_types.size()) {
    return false;
  }
  fori(return_types) {
    if (!return_types[i]->eql(*other.return_types[i].get_raw_ptr())) {
      return false;
    }
  }
  fori(arg_types) {
    if (!arg_types[i]->eql(*other.arg_types[i].get_raw_ptr())) {
      return false;
    }
  }
  return true;
}

u32 FunctionType::n_results() const {
  return return_types.size() == 1 && return_types[0]->is_void()
             ? 0
             : return_types.size();
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
    case AnyTypeType::Vector:
      return vec_u.v.eql(other.vec_u.v);
  }
}

u64 AnyType::hash() const {
  u64 h = static_cast<u64>(ty) + 1;
  switch (ty) {
    case AnyTypeType::Void:
    case AnyTypeType::Ptr:
      return h;
    case AnyTypeType::Integer:
      return hash_mix(h, int_u.v.bitwidth);
    case AnyTypeType::Float:
      return hash_mix(h, float_u.v.bitwidth);
    case AnyTypeType::Function:
      for (const auto &ret : func_u.v.return_types) {
        h = hash_mix(h, ret->hash());
      }
      // keeps (a) -> b and () -> a, b apart
      h = hash_mix(h, func_u.v.return_types.size());
      for (const auto &arg : func_u.v.arg_types) {
        h = hash_mix(h, arg->hash());
      }
      return h;
    case AnyTypeType::Vector:
      h = hash_mix(h, static_cast<u64>(vec_u.v.type));
      h = hash_mix(h, vec_u.v.bitwidth);
      return hash_mix(h, vec_u.v.member_number);
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
  }
}

AnyType::AnyType(AnyType &&old) noexcept : ty(old.ty) {
  switch (old.ty) {
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
      new (&func_u.v) auto(std::move(old.func_u.v));
      break;
    default:
      break;
  }

  old.ty = AnyTypeType::Void;
}

AnyType &AnyType::operator=(AnyType &&old) noexcept {
  if (this == &old) return *this;
  this->~AnyType();
  this->ty = old.ty;

  switch (old.ty) {
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
      new (&func_u.v) auto(std::move(old.func_u.v));
      break;
    default:
      break;
  }

  old.ty = AnyTypeType::Void;
  return *this;
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
      if (func.return_types.size() == 1) {
        app = fmt::format_to(app, col, "{}", func.return_types[0]);
      } else {
        app = fmt::format_to(app, col, "[");
        for (size_t i = 0; i < func.return_types.size(); i++) {
          if (i != 0) {
            app = fmt::format_to(app, col, " ");
          }
          app = fmt::format_to(app, col, "{}", func.return_types[i]);
        }
        app = fmt::format_to(app, col, "]");
      }
      app = fmt::format_to(app, col, "(");
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
  }
}
