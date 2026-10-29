#include "x86_codegen/inline_asm_emit.hpp"

#include <fmt/format.h>

#include <cstring>
#include <vector>

#include "utils/todo.hpp"
#include "x86_codegen/att_assembler.hpp"

namespace foptim::codegen {

namespace {

const char *gpr_name(fmir::CReg reg, u64 size) {
  using fmir::CReg;
  struct Names {
    const char *q, *d, *w, *b;
  };
  static const Names a{"rax", "eax", "ax", "al"}, b{"rbx", "ebx", "bx", "bl"},
      c{"rcx", "ecx", "cx", "cl"}, d{"rdx", "edx", "dx", "dl"},
      si{"rsi", "esi", "si", "sil"}, di{"rdi", "edi", "di", "dil"},
      r8{"r8", "r8d", "r8w", "r8b"}, r9{"r9", "r9d", "r9w", "r9b"},
      r10{"r10", "r10d", "r10w", "r10b"}, r11{"r11", "r11d", "r11w", "r11b"},
      r12{"r12", "r12d", "r12w", "r12b"}, r13{"r13", "r13d", "r13w", "r13b"},
      r14{"r14", "r14d", "r14w", "r14b"}, r15{"r15", "r15d", "r15w", "r15b"};
  const Names *n = nullptr;
  switch (reg) {
    case CReg::A:
      n = &a;
      break;
    case CReg::B:
      n = &b;
      break;
    case CReg::C:
      n = &c;
      break;
    case CReg::D:
      n = &d;
      break;
    case CReg::SI:
      n = &si;
      break;
    case CReg::DI:
      n = &di;
      break;
    case CReg::R8:
      n = &r8;
      break;
    case CReg::R9:
      n = &r9;
      break;
    case CReg::R10:
      n = &r10;
      break;
    case CReg::R11:
      n = &r11;
      break;
    case CReg::R12:
      n = &r12;
      break;
    case CReg::R13:
      n = &r13;
      break;
    case CReg::R14:
      n = &r14;
      break;
    case CReg::R15:
      n = &r15;
      break;
    default:
      TODO("inline asm operand in an unsupported register");
  }
  switch (size) {
    case 1:
      return n->b;
    case 2:
      return n->w;
    case 4:
      return n->d;
    default:
      return n->q;
  }
}

std::string operand_text(const fmir::MAsmBlock &block, size_t op_id,
                         const AsmOperandValue &val, char modifier,
                         const std::string &func_name) {
  const auto &arg = val.arg;
  const auto &op = block.operands[op_id];
  if (arg.isImm()) {
    const auto imm = static_cast<i64>(arg.imm);
    return modifier == 'c' || modifier == 'P' ? fmt::format("{}", imm)
                                              : fmt::format("${}", imm);
  }
  ASSERT(arg.isReg() && arg.reg.is_concrete());
  u64 size = fmir::get_size(arg.ty);
  switch (modifier) {
    case 'b':
      size = 1;
      break;
    case 'w':
      size = 2;
      break;
    case 'k':
      size = 4;
      break;
    case 'q':
      size = 8;
      break;
    case 0:
      break;
    default:
      fmt::println(stderr, "inline asm in {}: unsupported operand modifier {}",
                   func_name, modifier);
      IMPL("unsupported inline asm operand modifier");
  }
  if (op.cls == fir::AsmOperandClass::Mem) {
    return fmt::format("(%{})", gpr_name(arg.reg.c_reg(), 8));
  }
  return fmt::format("%{}", gpr_name(arg.reg.c_reg(), size));
}

}  // namespace

std::string expand_asm_template(const fmir::MAsmBlock &block,
                                std::span<const AsmOperandValue> operands,
                                u32 unique_id) {
  const std::string_view tmpl{block.tmpl};
  std::string res;
  size_t i = 0;
  while (i < tmpl.size()) {
    const char c = tmpl[i];
    // dialect alternatives, always att
    if (c == '{') {
      i++;
      continue;
    }
    if (c == '|') {
      while (i < tmpl.size() && tmpl[i] != '}') {
        i++;
      }
      continue;
    }
    if (c == '}') {
      i++;
      continue;
    }
    if (c != '$') {
      res += c;
      i++;
      continue;
    }
    i++;
    ASSERT_M(i < tmpl.size(), "inline asm template ends in $");
    if (tmpl[i] == '$') {
      res += '$';
      i++;
      continue;
    }
    if (tmpl[i] == '=') {
      res += fmt::format("{}", unique_id);
      i++;
      continue;
    }
    const bool braced = tmpl[i] == '{';
    if (braced) {
      i++;
    }
    char modifier = 0;
    if (braced && i < tmpl.size() && !(tmpl[i] >= '0' && tmpl[i] <= '9')) {
      modifier = tmpl[i++];
    }
    size_t op_id = 0;
    bool any = false;
    while (i < tmpl.size() && tmpl[i] >= '0' && tmpl[i] <= '9') {
      op_id = op_id * 10 + static_cast<size_t>(tmpl[i] - '0');
      any = true;
      i++;
    }
    if (braced && i < tmpl.size() && tmpl[i] == ':') {
      i++;
      modifier = tmpl[i++];
    }
    if (braced && i < tmpl.size() && tmpl[i] == '}') {
      i++;
    }
    ASSERT_M(any && op_id < operands.size() && operands[op_id].present,
             "inline asm template names an operand that does not exist");
    res += operand_text(block, op_id, operands[op_id], modifier, "");
  }
  return res;
}

size_t emit_inline_asm(const fmir::MFunc &func, const fmir::MBB &bb,
                       size_t indx, u8 *out_buff, u32 unique_id) {
  const auto &block = func.asm_blocks[bb.instrs[indx].args[0].imm];
  std::vector<AsmOperandValue> ops(block.operands.size());
  for (size_t j = indx;
       j > 0 && bb.instrs[j - 1].is(fmir::GBaseSubtype::asm_in); j--) {
    const auto &in = bb.instrs[j - 1];
    ops[in.args[1].imm] = {.present = true, .arg = in.args[0]};
  }
  for (size_t j = indx + 1;
       j < bb.instrs.size() && bb.instrs[j].is(fmir::GBaseSubtype::asm_out);
       j++) {
    const auto &out = bb.instrs[j];
    const auto op_id = out.args[1].imm;
    if (block.operands[op_id].kind == fir::AsmOperandKind::Output) {
      ops[op_id] = {.present = true, .arg = out.args[0]};
    }
  }
  const std::string text = expand_asm_template(block, ops, unique_id);
  std::vector<u8> bytes;
  std::string err;
  if (!assemble_att(text, bytes, err)) {
    fmt::println(stderr, "inline asm in {}: {}\n  asm: {}", func.name.c_str(),
                 err, text);
    IMPL("could not assemble inline asm");
  }
  if (!bytes.empty()) {
    memcpy(out_buff, bytes.data(), bytes.size());
  }
  return bytes.size();
}

}  // namespace foptim::codegen
