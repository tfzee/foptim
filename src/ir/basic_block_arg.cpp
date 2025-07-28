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
  return fmt::format_to(ctx.out(), "{:p}", (void *)v.get_raw_ptr());
}
