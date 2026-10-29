#pragma once
#include <span>
#include <string>

#include "mir/func.hpp"
#include "utils/types.hpp"

namespace foptim::codegen {

struct AsmOperandValue {
  bool present = false;
  fmir::MArgument arg;
};

// unique_id replaces $=
std::string expand_asm_template(const fmir::MAsmBlock &block,
                                std::span<const AsmOperandValue> operands,
                                u32 unique_id);

// Assembles the inline_asm at bb.instrs[indx] into out_buff, returns the size.
size_t emit_inline_asm(const fmir::MFunc &func, const fmir::MBB &bb,
                       size_t indx, u8 *out_buff, u32 unique_id);

}  // namespace foptim::codegen
