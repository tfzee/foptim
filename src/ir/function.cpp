
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
  utils::Debug << "==\nBLOCK:\n"<< b << "\nIN FUNCTION:\n"<< *this << "==\n";
  ASSERT_M(false, "Tried to get bb_id of block that is not in this function");
  std::abort();
}

bool Function::verify(utils::Printer printer) const {
  if (basic_blocks.empty() || !get_entry().is_valid())  {
    printer << "Function atleast needs an entry block";
    return false;
  }
  if (!func_ty.is_valid()) {
    printer << "Function type invalid";
    return false;
  }
  const auto &ty = func_ty->as_func_ty();

  auto entry = get_entry(); 
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
