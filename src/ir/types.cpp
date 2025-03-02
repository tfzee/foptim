#include "ir/types.hpp"

namespace foptim::fir {} // namespace foptim::fir

fmt::appender
fmt::formatter<foptim::fir::TypeR>::format(foptim::fir::TypeR const &v,
                                           format_context &ctx) const {
  constexpr auto col = fg(fmt::color::light_coral);
  if (!v.is_valid()) {
    return fmt::format_to(ctx.out(), col, "INVALID");
  }
  return std::visit(
      [&ctx, col](auto &&v) {
        if constexpr (typeid(v) == typeid(foptim::fir::IntegerType)) {
          return fmt::format_to(ctx.out(), col, "i{}", v.bitwidth);
        } else if constexpr (typeid(v) == typeid(foptim::fir::FloatType)) {
          return fmt::format_to(ctx.out(), col, "f{}", v.bitwidth);
        } else if constexpr (typeid(v) == typeid(foptim::fir::VoidType)) {
          return fmt::format_to(ctx.out(), col, "()");
        } else if constexpr (typeid(v) ==
                             typeid(foptim::fir::OpaquePointerType)) {
          return fmt::format_to(ctx.out(), col, "ptr");
        } else {
          return fmt::format_to(ctx.out(), col, "{}", typeid(v).name());
        }
      },
      v->type);
}
