#include "ir/basic_block_arg.hpp"
// #include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/types_ref.hpp"

namespace foptim::fir {

BBArgumentData::BBArgumentData(BasicBlock parent, TypeR type)
    : Used(), _parent(parent), _type(type) {}

[[nodiscard]] BasicBlock BBArgumentData::get_parent() const { return _parent; }
[[nodiscard]] TypeR BBArgumentData::get_type() const { return _type; }
// void BBArgumentData::remove_from_parent() {
//   ASSERT(_parent.is_valid());
//   int i = 0;
//   for (auto arg : _parent->args) {
//     if (arg.get_raw_ptr() == this) {
//       _parent->args.erase(_parent->args.begin() + i);
//       _parent = BasicBlock{BasicBlock::invalid()};
//     }
//     i++;
//   }
//   TODO("idk didnt find that no good");
// }

}  // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::BBArgument>::format(
    foptim::fir::BBArgument const &v, format_context &ctx) const {
  auto colv2 = color ? color_value2 : text_style{};
  auto f = ctx.out();
  if (!extended) {
    return fmt::format_to(f, colv2, "{:p}", (void *)v.get_raw_ptr());
  }
  f = fmt::format_to(f, colv2, "{:p}", (void *)v.get_raw_ptr());
  fmt::format_to(ctx.out(), "{{");
  if (v->noalias) {
    f = fmt::format_to(f, "noalias; ");
  }
  if (!v->get_attribs().empty()) {
    const auto &attribs = v->get_attribs();
    for (auto [key, value] : attribs) {
      fmt::format_to(ctx.out(), "{}{}; ", key.c_str(), value);
    }
  }
  fmt::format_to(ctx.out(), "}}");
  if (color) {
    f = fmt::format_to(f, ": {:c}", v->get_type());
  } else {
    f = fmt::format_to(f, ": {}", v->get_type());
  }
  if (debug) {
    f = fmt::format_to(f, " NUSES: {}", v->get_n_uses());
  }
  return f;
}
