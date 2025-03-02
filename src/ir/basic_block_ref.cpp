#include "basic_block_ref.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::fir {

Builder BasicBlock::builder() { return {*this}; }

Builder BasicBlock::builder_at_end() {
  Builder b = {*this};
  b.at_end(*this);
  return b;
}

void BasicBlock::insert_instr(size_t indx, Instr instr) {
  auto *self = operator->();
  instr->set_parent(*this);
  self->instructions.insert(self->instructions.begin() + indx, instr);
}

void BasicBlock::push_instr(Instr instr) {
  auto *self = operator->();
  instr->set_parent(*this);
  self->instructions.push_back(instr);
}

BBArgument BasicBlock::add_arg(BBArgument arg) {
  auto *self = operator->();
  arg->_parent = *this;
  self->args.push_back(arg);
  return arg;
}

} // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::BasicBlock>::format(
    foptim::fir::BasicBlock const &bb, format_context &ctx) const {
  fmt::format_to(ctx.out(), "{:p}(", (void *)bb.get_raw_ptr());
  const auto &args = bb->args;
  if (args.size() > 0) {
    fmt::format_to(ctx.out(), "{}: {}", args[0], args[0]->get_type());
    if (!args[0]->get_attribs().empty()) {
      fmt::format_to(ctx.out(), "{{");
      const auto &attribs = args[0]->get_attribs();
      for (auto [key, value] : attribs) {
        fmt::format_to(ctx.out(), "{}{}, ", key.c_str(), value);
      }
      fmt::format_to(ctx.out(), "}}");
    }
    for (size_t i = 1; i < args.size(); i++) {
      fmt::format_to(ctx.out(), ", {}: {}", args[i], args[i]->get_type());
      if (!args[i]->get_attribs().empty()) {
        fmt::format_to(ctx.out(), "{{");
        const auto &attribs = args[i]->get_attribs();
        for (auto [key, value] : attribs) {
          fmt::format_to(ctx.out(), "{}{}, ", key.c_str(), value);
        }
        fmt::format_to(ctx.out(), "}}");
      }
    }
  }

  fmt::format_to(ctx.out(), "):\n");
  for (foptim::fir::Instr instr : bb->get_instrs()) {
    fmt::format_to(ctx.out(), "    {}", instr);
  }
  return ctx.out();
}
