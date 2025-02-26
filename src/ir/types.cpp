#include "ir/types.hpp"

namespace foptim::fir {} // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::TypeR>::format(foptim::fir::TypeR const &v,
                                                format_context &ctx) const {
  return std::visit(
      [&ctx](auto &&v) {
        if constexpr (typeid(v) == typeid(foptim::fir::IntegerType)) {
          return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "i{}", v.bitwidth);
        } else if constexpr (typeid(v) == typeid(foptim::fir::FloatType)) {
          return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "f{}", v.bitwidth);
        } else if constexpr (typeid(v) == typeid(foptim::fir::VoidType)) {
          return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "()");
        } else if constexpr (typeid(v) ==
                             typeid(foptim::fir::OpaquePointerType)) {
          return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "ptr");
        } else {
          return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "{}", typeid(v).name());
        }
      },
      v->type);
}
