#include <fmt/color.h>

#include "builder.hpp"
#include "ir/basic_block_ref.hpp"
#include "utils/logging.hpp"
#include "utils/todo.hpp"

namespace foptim::fir {

Builder FunctionR::builder() { return Builder{*this}; }

void Function::dump_data_dependency_graph(const char *filename) const {
  auto *file = std::fopen(filename, "w");
  fmt::println(file, "digraph G{{");
  fmt::println(file, "node [shape=box, fontname=\"monospace\", fontsize=10];");
  for (auto b : basic_blocks) {
    for (auto arg : b->args) {
      fmt::println(file, R"( "{}"[label="{}"])", (void *)arg.get_raw_ptr(),
                   (void *)arg.get_raw_ptr());
      for (auto u : arg->get_uses()) {
        fmt::print(file, "\"{}\" -> \"{}\" [color=\"red\"];\n",
                   (void *)arg.get_raw_ptr(), (void *)u.user.get_raw_ptr());
      }
    }
    for (auto i : b->instructions) {
      fmt::print(file, R"( "{}"[label="{}"])", (void *)i.get_raw_ptr(), i);
      for (auto u : i->get_uses()) {
        fmt::print(file, "\"{}\" -> \"{}\" [color=\"red\"];\n",
                   (void *)i.get_raw_ptr(), (void *)u.user.get_raw_ptr());
      }
    }
  }
  fmt::print(file, "}}\n");
  fclose(file);
}

[[nodiscard]] size_t Function::bb_id(BasicBlock b) const {
  for (size_t bb_indx = 0; bb_indx < basic_blocks.size(); bb_indx++) {
    if (basic_blocks[bb_indx] == b) {
      return bb_indx;
    }
  }

  fmt::println("BLOCK: {}\nIN FUNCTION {}", b, *this);
  ASSERT_M(false, "Tried to get bb_id of block that is not in this function");
  std::abort();
}

bool Function::verify() const {
  if (basic_blocks.empty()) {
    return true;
  }
  if (!get_entry().is_valid()) {
    fmt::println("Function atleast needs an entry block");
    return false;
  }
  if (!func_ty.is_valid()) {
    fmt::println("Function type invalid");
    return false;
  }
  const auto &ty = func_ty->as_func();

  auto entry = get_entry();
  if (ty.arg_types.size() != entry->n_args()) {
    fmt::println("Entry basic block needs as many arguments as function");
    return false;
  }
  for (size_t i = 0; i < ty.arg_types.size(); i++) {
    if (!ty.arg_types[i]->eql(*entry->args[i]->get_type().get_raw_ptr())) {
      fmt::println(
          "Argument type at location {} does not match the type of "
          "the function {} != {}",
          i, ty.arg_types[i], entry->args[i]->get_type());
      return false;
    }
  }

  for (const auto &bb : basic_blocks) {
    if (!bb.is_valid() || !bb->verify(this)) {
      fmt::println("In BB {:p}", (void *)bb.get_raw_ptr());
      return false;
    }
  }
  return true;
}
}  // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::Function>::format(
    foptim::fir::Function const &func, format_context &ctx) const {
  auto app = ctx.out();
  app = fmt::format_to(app, "\n; ");
  if (func.variadic) {
    app = fmt::format_to(app, "VARIADIC, ");
  }
  if (func.must_progress) {
    app = fmt::format_to(app, "MUST_PROGRESS, ");
  }
  if (func.no_inline) {
    app = fmt::format_to(app, "NO_INLINE, ");
  }
  if (func.must_inline) {
    app = fmt::format_to(app, "MUST_INLINE, ");
  }
  if (func.no_recurse) {
    app = fmt::format_to(app, "NO_RECURSE, ");
  }
  if (func.mem_read_none) {
    app = fmt::format_to(app, "MEM(NONE), ");
  }
  if (func.mem_read_only) {
    app = fmt::format_to(app, "MEM(READ), ");
  }
  auto colfunc = color ? color_func : text_style{};
  app = fmt::format_to(app, "\nfunc {}",
                       fmt::styled(func.getName().c_str(), colfunc));

  app = fmt::format_to(app, "<CC: ");
  switch (func.cc) {
    case foptim::fir::Function::CallingConv::C:
      app = fmt::format_to(app, "C");
      break;
    case foptim::fir::Function::CallingConv::Dynamic:
      app = fmt::format_to(app, "dyn");
      break;
  }
  app = fmt::format_to(app, ", LINK: ");
  switch (func.linkage) {
    case foptim::fir::Linkage::Internal:
      app = fmt::format_to(app, "internal");
      break;
    case foptim::fir::Linkage::External:
      app = fmt::format_to(app, "external");
      break;
    case foptim::fir::Linkage::Weak:
      app = fmt::format_to(app, "weak");
      break;
    case foptim::fir::Linkage::WeakODR:
      app = fmt::format_to(app, "weakODR");
      break;
    case foptim::fir::Linkage::LinkOnce:
      app = fmt::format_to(app, "linkonce");
      break;
    case foptim::fir::Linkage::LinkOnceODR:
      app = fmt::format_to(app, "linkonceODR");
      break;
  }
  app = fmt::format_to(app, ", ");
  const auto &attribs = func.get_attribs();
  for (auto [key, value] : attribs) {
    app = fmt::format_to(app, "  {}{}, ", key.c_str(), value);
  }

  app = fmt::format_to(app, ">");
  if (debug) {
    auto coldebug = color ? color_debug : text_style{};
    app = fmt::format_to(app, coldebug, " Uses: {}", func.get_n_uses());
  }
  if (!func.get_bbs().empty()) {
    app = fmt::format_to(app, "\n{{\n");
    for (foptim::fir::BasicBlock bb : func.get_bbs()) {
      if (debug && color) {
        app = fmt::format_to(app, "  {:cd}", bb);
      } else if (color) {
        app = fmt::format_to(app, "  {:c}", bb);
      } else if (debug) {
        app = fmt::format_to(app, "  {:d}", bb);
      } else {
        app = fmt::format_to(app, "  {}", bb);
      }
    }
    app = fmt::format_to(app, "}}");
  } else {
    app = fmt::format_to(app, " {{}}");
  }
  return app;
}
