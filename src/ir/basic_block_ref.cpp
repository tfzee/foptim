#include <fmt/color.h>
#include <fmt/core.h>

#include "basic_block_ref.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "utils/logging.hpp"

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

}  // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::BasicBlock>::format(
    foptim::fir::BasicBlock const &bb, format_context &ctx) const {
  auto colbb = color ? color_bb : text_style{};

  fmt::format_to(ctx.out(), colbb, "{:p}", (void *)bb.get_raw_ptr());
  fmt::format_to(ctx.out(), "(");
  const auto &args = bb->args;
  if (args.size() > 0) {
    if (color && debug) {
      fmt::format_to(ctx.out(), "{:ecd}", args[0]);
    } else if (color) {
      fmt::format_to(ctx.out(), "{:ec}", args[0]);
    } else {
      fmt::format_to(ctx.out(), "{:e}", args[0]);
    }
    for (size_t i = 1; i < args.size(); i++) {
      if (color && debug) {
        fmt::format_to(ctx.out(), ", {:ecd}", args[i]);
      } else if (color) {
        fmt::format_to(ctx.out(), ", {:ec}", args[i]);
      } else {
        fmt::format_to(ctx.out(), ", {:e}", args[i]);
      }
    }
  }

  fmt::format_to(ctx.out(), "):\n");
  for (foptim::fir::Instr instr : bb->get_instrs()) {
    if (debug && color) {
      fmt::format_to(ctx.out(), "    {:cd}\n", instr);
    } else if (debug) {
      fmt::format_to(ctx.out(), "    {:d}\n", instr);
    } else if (color) {
      fmt::format_to(ctx.out(), "    {:c}\n", instr);
    } else {
      fmt::format_to(ctx.out(), "    {}\n", instr);
    }
  }
  return ctx.out();
}
