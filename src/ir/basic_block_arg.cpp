#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/types_ref.hpp"

namespace foptim::fir {

BBArgumentData::BBArgumentData(BasicBlock parent, TypeR type)
    : Used(), _parent(parent), _type(type) {}

[[nodiscard]] BasicBlock BBArgumentData::get_parent() const { return _parent; }
[[nodiscard]] TypeR BBArgumentData::get_type() const { return _type; }

} // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::BBArgument>::format(
    foptim::fir::BBArgument const &v, format_context &ctx) const {
  return fmt::format_to(ctx.out(), "{:p}", (void *)v.get_raw_ptr());
}
