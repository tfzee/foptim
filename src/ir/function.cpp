#include "builder.hpp"
#include "utils/logging.hpp"
#include "utils/todo.hpp"

namespace foptim::fir {

Builder FunctionR::builder() { return Builder{*this}; }

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
    if (ty.arg_types[i] != entry->args[i]->get_type()) {
      fmt::println("Argument type at location {} does not match the type of "
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
} // namespace foptim::fir

fmt::appender
fmt::formatter<foptim::fir::Function>::format(foptim::fir::Function const &func,
                                              format_context &ctx) const {
  auto app = ctx.out();
  app = fmt::format_to(app, "\nfunc {}", func.getName().c_str());

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
  case foptim::fir::Function::Linkage::Internal:
    app = fmt::format_to(app, "internal");
    break;
  case foptim::fir::Function::Linkage::External:
    app = fmt::format_to(app, "external");
    break;
  }
  app = fmt::format_to(app, ", ");
  const auto &attribs = func.get_attribs();
  for (auto [key, value] : attribs) {
    app = fmt::format_to(app, "  {}{}, ", key.c_str(), value);
  }

  app = fmt::format_to(app, ">");
  if (debug) {
    app = fmt::format_to(app, "Uses: {}", func.get_n_uses());
  }
  if (!func.get_bbs().empty()) {
    app = fmt::format_to(app, "\n{{\n");
    for (foptim::fir::BasicBlock bb : func.get_bbs()) {
      app = fmt::format_to(app, "  {}", bb);
    }
    app = fmt::format_to(app, "}}");
  } else {
    app = fmt::format_to(app, "\n{{}}");
  }
  return app;
}
