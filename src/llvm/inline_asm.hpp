#pragma once
#include <llvm/IR/InlineAsm.h>

#include "ir/inline_asm.hpp"

namespace foptim {

// Parses the constraint string of an inline asm. Constraints the backend does
// not support abort with the asm string.
fir::AsmInfo parse_inline_asm(const llvm::InlineAsm &as,
                              llvm::StringRef function_name);

}  // namespace foptim
