#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "utils/types.hpp"

namespace foptim::codegen {

// Assembles AT&T text into position independent machine code, labels inside the
// text are resolved and anything that needs a relocation is an error. The only
// LLVM MC user of the backend.
bool assemble_att(std::string_view text, std::vector<u8> &out,
                  std::string &err);

}  // namespace foptim::codegen
