#include "llvm/inline_asm.hpp"

#include <fmt/format.h>

#include "utils/todo.hpp"

namespace foptim {

namespace {

[[noreturn]] void unsupported(const llvm::InlineAsm &as,
                              llvm::StringRef function_name,
                              const std::string &what) {
  fmt::println(stderr, "inline asm in {}: {}\n  asm: {}\n  constraints: {}",
               function_name.str(), what, as.getAsmString(),
               as.getConstraintString());
  IMPL("unsupported inline asm constraint");
  __builtin_unreachable();
}

// {ax} -> ax
std::string register_name(const std::string &code) {
  return code.substr(1, code.size() - 2);
}

}  // namespace

fir::AsmInfo parse_inline_asm(const llvm::InlineAsm &as,
                              llvm::StringRef function_name) {
  using fir::AsmOperand;
  using fir::AsmOperandClass;
  using fir::AsmOperandKind;

  fir::AsmInfo info;
  info.tmpl = IRString{as.getAsmString()};
  info.constraints = IRString{as.getConstraintString()};
  info.sideeffect = as.hasSideEffects();
  info.alignstack = as.isAlignStack();
  if (as.getDialect() != llvm::InlineAsm::AD_ATT) {
    unsupported(as, function_name, "only the AT&T dialect is supported");
  }
  if (as.canThrow()) {
    unsupported(as, function_name, "asm that can throw");
  }

  const auto constraints =
      llvm::InlineAsm::ParseConstraints(as.getConstraintString());
  i32 next_arg = 0;
  for (const auto &c : constraints) {
    AsmOperand op;
    switch (c.Type) {
      case llvm::InlineAsm::isOutput:
        op.kind = AsmOperandKind::Output;
        break;
      case llvm::InlineAsm::isInput:
        op.kind = AsmOperandKind::Input;
        break;
      case llvm::InlineAsm::isClobber:
        op.kind = AsmOperandKind::Clobber;
        break;
      case llvm::InlineAsm::isLabel:
        unsupported(as, function_name, "asm goto");
    }
    if (c.isMultipleAlternative) {
      unsupported(as, function_name, "multiple alternative constraints");
    }
    if (c.Codes.empty()) {
      unsupported(as, function_name, "constraint without a code");
    }
    op.early_clobber = c.isEarlyClobber;
    op.indirect = c.isIndirect;
    // the first code decides, so rm and g take the first fitting class
    const std::string &code = c.Codes[0];

    if (op.kind == AsmOperandKind::Clobber) {
      const std::string name = register_name(code);
      if (name == "memory") {
        info.mem_clobber = true;
        continue;
      }
      // flags are not tracked, dirflag and fpsr are never changed by the asm
      // we support
      if (name == "cc" || name == "flags" || name == "dirflag" ||
          name == "fpsr") {
        continue;
      }
      op.cls = AsmOperandClass::Fixed;
      op.reg = IRString{name};
      info.operands.push_back(std::move(op));
      continue;
    }

    if (code.size() > 2 && code.front() == '{') {
      op.cls = AsmOperandClass::Fixed;
      op.reg = IRString{register_name(code)};
    } else if (code.size() == 1 && code[0] >= '0' && code[0] <= '9') {
      // tied input, takes the register of the output it names
      if (op.kind != AsmOperandKind::Input) {
        unsupported(as, function_name, "matching constraint on an output");
      }
      op.cls = AsmOperandClass::Reg;
    } else if (code.size() == 1) {
      switch (code[0]) {
        case 'r':
        case 'q':
        case 'Q':
        case 'R':
        case 'l':
        case 'g':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'S':
        case 'D':
          op.cls = AsmOperandClass::Reg;
          break;
        case 'm':
        case 'o':
        case 'V':
          op.cls = AsmOperandClass::Mem;
          break;
        case 'i':
        case 'n':
        case 'e':
        case 'Z':
        case 'I':
        case 'J':
        case 'K':
        case 'L':
        case 'M':
        case 'N':
        case 'O':
          op.cls = AsmOperandClass::Imm;
          break;
        default:
          unsupported(as, function_name,
                      fmt::format("constraint code '{}'", code));
      }
      // the one letter register constraints name a register
      static const char *const fixed_names[] = {"ax", "bx", "cx",
                                                "dx", "si", "di"};
      static const char fixed_codes[] = {'a', 'b', 'c', 'd', 'S', 'D'};
      for (size_t i = 0; i < 6; i++) {
        if (code[0] == fixed_codes[i]) {
          op.cls = AsmOperandClass::Fixed;
          op.reg = IRString{fixed_names[i]};
        }
      }
    } else {
      unsupported(as, function_name, fmt::format("constraint code '{}'", code));
    }
    if (op.cls == AsmOperandClass::Mem || op.indirect) {
      op.cls = AsmOperandClass::Mem;
      if (op.kind == AsmOperandKind::Output) {
        info.mem_clobber = true;
      }
    }
    if (c.hasArg()) {
      op.arg = next_arg++;
    }
    info.operands.push_back(std::move(op));
  }

  // tie the outputs to the inputs that name them
  const i32 n_ops = static_cast<i32>(info.operands.size());
  for (i32 i = 0; i < n_ops; i++) {
    if (info.operands[i].kind != AsmOperandKind::Output) {
      continue;
    }
    // MatchingInput indexes the full constraint list, clobbers dropped from
    // operands come last so the indices still line up
    const int m = constraints[i].MatchingInput;
    if (m >= 0) {
      info.operands[i].tied = m;
    }
  }
  return info;
}

}  // namespace foptim
