
#include "builder.hpp"
#include "utils/logging.hpp"

namespace foptim::fir {

Builder FunctionR::builder() { return Builder{*this}; }

bool Function::verify(utils::Printer printer) const {
  if (!entry.is_valid()) {
    printer << "Entry block needs to be set for function";
    return false;
  }
  if (!func_ty.is_valid()) {
    printer << "Function type invalid";
    return false;
  }
  auto ty = func_ty->as_func_ty();

  if (ty.arg_types.size() != entry->n_args()) {
    printer << "Entry basic block needs as many arguments as function";
    return false;
  }
  for (size_t i = 0; i < ty.arg_types.size(); i++) {
    if (ty.arg_types[i] != entry->args[i].type) {
      printer << "Argument type at location " << i
              << " does not match the type of the function" << ty.arg_types[i]
              << " != " << entry->args[i].type;
      return false;
    }
  }

  for (const auto &bb : basic_blocks) {

    if (!bb.is_valid() || !bb->verify(this, printer)) {
      return false;
    }
  }
  (void)printer;
  return true;
}
} // namespace foptim::fir
