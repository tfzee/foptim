#pragma once
#include <functional>
#include <string_view>

#include "utils/string.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

enum class AsmOperandKind : u8 { Output, Input, Clobber };

enum class AsmOperandClass : u8 {
  Reg,
  // address of a memory location, the operand is the pointer
  Mem,
  Imm,
  // the register named in AsmOperand::reg
  Fixed,
};

struct AsmOperand {
  AsmOperandKind kind = AsmOperandKind::Input;
  AsmOperandClass cls = AsmOperandClass::Reg;
  // output written before all inputs are read
  bool early_clobber = false;
  // output: written through the pointer operand, input: memory operand
  bool indirect = false;
  // output: index into AsmInfo::operands of the input that shares its register
  // (+ constraint), -1 for none
  i32 tied = -1;
  // index of the FIR operand after the id (inputs and indirect outputs), the
  // direct outputs are the results in order, -1 for clobbers
  i32 arg = -1;
  // register name of a Fixed operand or a register clobber, like ax or rdx
  IRString reg;
};

// One inline asm statement, interned per module, the instruction holds the id.
struct AsmInfo {
  IRString tmpl;
  IRString constraints;
  bool sideeffect = false;
  bool alignstack = false;
  // memory clobber or a memory operand that is written
  bool mem_clobber = false;
  IRVec<AsmOperand> operands;

  [[nodiscard]] bool same_key(const AsmInfo &o) const {
    return tmpl == o.tmpl && constraints == o.constraints &&
           sideeffect == o.sideeffect && alignstack == o.alignstack;
  }
  [[nodiscard]] u64 hash_key() const {
    u64 h = std::hash<std::string_view>{}(std::string_view{tmpl});
    h = h * 31 + std::hash<std::string_view>{}(std::string_view{constraints});
    return h * 4 + (sideeffect ? 1 : 0) + (alignstack ? 2 : 0);
  }
};

}  // namespace foptim::fir
