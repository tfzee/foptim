#include "backend.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "third_party/Zydis.h"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include "utils/parameters.hpp"
#include <elfio/elf_types.hpp>
#include <elfio/elfio.hpp>

namespace foptim::codegen {

utils::Printer operator<<(utils::Printer p, const ZydisEncoderOperand &data);
utils::Printer operator<<(utils::Printer p, const ZydisRegister &data);

enum class RelocSection : u8 {
  INVALID,
  Data,
  Text,
  Extern,
};
enum class RelocKind : u8 {
  INVALID,
  BB,
  Func,
  Data,
};

struct LabelRelocData {
  struct Usage {
    // ptr to instruction
    u8 *usage_instr;
    u8 operand_num;
    RelocSection usage_section = RelocSection::INVALID;
  };

  // absolute location *inside* of the section
  RelocKind kind = RelocKind::INVALID;
  RelocSection section = RelocSection::INVALID;
  u64 def_loc = 0;
  u64 size = 0;
  TVec<Usage> usage_loc;
};

struct TLabelUsageMap {
  TMap<IRString, LabelRelocData> label_map;
  TMap<u32, LabelRelocData> bb_map;

  void insert_label_ref(const IRString &label, u8 *instr_loc, u8 op_num,
                        RelocSection section) {
    label_map[label].usage_loc.push_back({instr_loc, op_num, section});
  }
  void insert_bb_ref(const u32 bb, u8 *instr_loc, u8 op_num,
                     RelocSection section) {
    bb_map[bb].usage_loc.push_back({instr_loc, op_num, section});
  }
};

ZydisRegister get_reg_sized(const ZydisRegister *regs, u32 size) {
  switch (size) {
  case 1:
    return regs[0];
  case 2:
    return regs[1];
  case 4:
    return regs[2];
  case 8:
    return regs[3];
  default:
  }
  utils::Debug << "Got size: " << size << " but only 1,2,4,8 is valid\n";
  ASSERT_M(false, "Tried to get invalid reg size");
  std::abort();
}

ZydisRegister convert_reg(const fmir::VReg &reg) {
  constexpr ZydisRegister a_regs[] = {ZYDIS_REGISTER_AL, ZYDIS_REGISTER_AX,
                                      ZYDIS_REGISTER_EAX, ZYDIS_REGISTER_RAX};
  constexpr ZydisRegister b_regs[] = {ZYDIS_REGISTER_BL, ZYDIS_REGISTER_BX,
                                      ZYDIS_REGISTER_EBX, ZYDIS_REGISTER_RBX};
  constexpr ZydisRegister c_regs[] = {ZYDIS_REGISTER_CL, ZYDIS_REGISTER_CX,
                                      ZYDIS_REGISTER_ECX, ZYDIS_REGISTER_RCX};
  constexpr ZydisRegister d_regs[] = {ZYDIS_REGISTER_DL, ZYDIS_REGISTER_DX,
                                      ZYDIS_REGISTER_EDX, ZYDIS_REGISTER_RDX};
  constexpr ZydisRegister di_regs[] = {ZYDIS_REGISTER_DIL, ZYDIS_REGISTER_DI,
                                       ZYDIS_REGISTER_EDI, ZYDIS_REGISTER_RDI};
  constexpr ZydisRegister si_regs[] = {ZYDIS_REGISTER_SIL, ZYDIS_REGISTER_SI,
                                       ZYDIS_REGISTER_ESI, ZYDIS_REGISTER_RSI};
  constexpr ZydisRegister sp_regs[] = {ZYDIS_REGISTER_SPL, ZYDIS_REGISTER_SP,
                                       ZYDIS_REGISTER_ESP, ZYDIS_REGISTER_RSP};
  constexpr ZydisRegister bp_regs[] = {ZYDIS_REGISTER_BPL, ZYDIS_REGISTER_BP,
                                       ZYDIS_REGISTER_EBP, ZYDIS_REGISTER_RBP};
  constexpr ZydisRegister r8_regs[] = {ZYDIS_REGISTER_R8B, ZYDIS_REGISTER_R8W,
                                       ZYDIS_REGISTER_R8D, ZYDIS_REGISTER_R8};
  constexpr ZydisRegister r9_regs[] = {ZYDIS_REGISTER_R9B, ZYDIS_REGISTER_R9W,
                                       ZYDIS_REGISTER_R9D, ZYDIS_REGISTER_R9};
  constexpr ZydisRegister r10_regs[] = {
      ZYDIS_REGISTER_R10B, ZYDIS_REGISTER_R10W, ZYDIS_REGISTER_R10D,
      ZYDIS_REGISTER_R10};
  constexpr ZydisRegister r11_regs[] = {
      ZYDIS_REGISTER_R11B, ZYDIS_REGISTER_R11W, ZYDIS_REGISTER_R11D,
      ZYDIS_REGISTER_R11};
  constexpr ZydisRegister r12_regs[] = {
      ZYDIS_REGISTER_R12B, ZYDIS_REGISTER_R12W, ZYDIS_REGISTER_R12D,
      ZYDIS_REGISTER_R12};
  constexpr ZydisRegister r13_regs[] = {
      ZYDIS_REGISTER_R13B, ZYDIS_REGISTER_R13W, ZYDIS_REGISTER_R13D,
      ZYDIS_REGISTER_R13};
  constexpr ZydisRegister r14_regs[] = {
      ZYDIS_REGISTER_R14B, ZYDIS_REGISTER_R14W, ZYDIS_REGISTER_R14D,
      ZYDIS_REGISTER_R14};
  constexpr ZydisRegister r15_regs[] = {
      ZYDIS_REGISTER_R15B, ZYDIS_REGISTER_R15W, ZYDIS_REGISTER_R15D,
      ZYDIS_REGISTER_R15};

  switch (reg.info.ty) {
  case fmir::VRegType::A:
    return get_reg_sized(a_regs, reg.info.reg_size);
  case fmir::VRegType::B:
    return get_reg_sized(b_regs, reg.info.reg_size);
  case fmir::VRegType::C:
    return get_reg_sized(c_regs, reg.info.reg_size);
  case fmir::VRegType::D:
    return get_reg_sized(d_regs, reg.info.reg_size);
  case fmir::VRegType::DI:
    return get_reg_sized(di_regs, reg.info.reg_size);
  case fmir::VRegType::SI:
    return get_reg_sized(si_regs, reg.info.reg_size);
  case fmir::VRegType::SP:
    return get_reg_sized(sp_regs, reg.info.reg_size);
  case fmir::VRegType::BP:
    return get_reg_sized(bp_regs, reg.info.reg_size);
  case fmir::VRegType::R8:
    return get_reg_sized(r8_regs, reg.info.reg_size);
  case fmir::VRegType::R9:
    return get_reg_sized(r9_regs, reg.info.reg_size);
  case fmir::VRegType::R10:
    return get_reg_sized(r10_regs, reg.info.reg_size);
  case fmir::VRegType::R11:
    return get_reg_sized(r11_regs, reg.info.reg_size);
  case fmir::VRegType::R12:
    return get_reg_sized(r12_regs, reg.info.reg_size);
  case fmir::VRegType::R13:
    return get_reg_sized(r13_regs, reg.info.reg_size);
  case fmir::VRegType::R14:
    return get_reg_sized(r14_regs, reg.info.reg_size);
  case fmir::VRegType::R15:
    return get_reg_sized(r15_regs, reg.info.reg_size);
  case fmir::VRegType::mm0:
    return ZYDIS_REGISTER_XMM0;
  case fmir::VRegType::mm1:
    return ZYDIS_REGISTER_XMM1;
  case fmir::VRegType::mm2:
    return ZYDIS_REGISTER_XMM2;
  case fmir::VRegType::mm3:
    return ZYDIS_REGISTER_XMM3;
  case fmir::VRegType::mm4:
    return ZYDIS_REGISTER_XMM4;
  case fmir::VRegType::mm5:
    return ZYDIS_REGISTER_XMM5;
  case fmir::VRegType::mm6:
    return ZYDIS_REGISTER_XMM6;
  case fmir::VRegType::mm7:
    return ZYDIS_REGISTER_XMM7;
  case fmir::VRegType::mm8:
    return ZYDIS_REGISTER_XMM8;
  case fmir::VRegType::mm9:
    return ZYDIS_REGISTER_XMM9;
  case fmir::VRegType::mm10:
    return ZYDIS_REGISTER_XMM10;
  case fmir::VRegType::mm11:
    return ZYDIS_REGISTER_XMM11;
  case fmir::VRegType::mm12:
    return ZYDIS_REGISTER_XMM12;
  case fmir::VRegType::mm13:
    return ZYDIS_REGISTER_XMM13;
  case fmir::VRegType::mm14:
    return ZYDIS_REGISTER_XMM14;
  case fmir::VRegType::mm15:
    return ZYDIS_REGISTER_XMM15;
  case fmir::VRegType::N_REGS:
  case fmir::VRegType::Virtual:
    UNREACH();
  }
}

ZydisRegister reg_with_size(fmir::VReg reg, u8 new_size) {
  reg.info.reg_size = new_size;
  return convert_reg(reg);
}

void emit_operand(fmir::MArgument &arg, ZydisEncoderOperand &operand,
                  TLabelUsageMap &reloc_map, u8 *instr_ptr, u8 arg_id) {
  switch (arg.type) {
  case fmir::MArgument::ArgumentType::Imm:
    operand.type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    if (arg.is_fp()) {
    } else {
      switch (arg.ty) {
      case fmir::Type::INVALID:
      case fmir::Type::Int8:
        operand.imm.s = (i8)arg.imm;
        break;
      case fmir::Type::Int16:
        operand.imm.s = (i16)arg.imm;
        break;
      case fmir::Type::Int32:
        operand.imm.s = (i32)arg.imm;
        break;
      case fmir::Type::Int64:
        operand.imm.s = (i64)arg.imm;
        break;
      case fmir::Type::Float32:
        operand.imm.s = (u32)arg.immf;
        break;
      case fmir::Type::Float64:
        operand.imm.u = (u64)arg.immf;
        break;
      }
    }
    return;
  case fmir::MArgument::ArgumentType::VReg:
    operand.type = ZYDIS_OPERAND_TYPE_REGISTER;
    operand.reg.value = convert_reg(arg.reg);
    // TODO?
    operand.reg.is4 = 0U;
    return;
  case fmir::MArgument::ArgumentType::Label:
    reloc_map.insert_label_ref(arg.label, instr_ptr, arg_id,
                               RelocSection::Text);
    operand.type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    operand.imm.s = 0;
    return;
  case fmir::MArgument::ArgumentType::MemLabel:
    reloc_map.insert_label_ref(arg.label, instr_ptr, arg_id,
                               RelocSection::Text);
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = ZYDIS_REGISTER_RIP;
    operand.mem.displacement = 0;
    operand.mem.index = ZYDIS_REGISTER_NONE;
    operand.mem.scale = 0;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemImmLabel:
    reloc_map.insert_label_ref(arg.label, instr_ptr, arg_id,
                               RelocSection::Text);
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = ZYDIS_REGISTER_RIP;
    operand.mem.displacement = arg.imm;
    operand.mem.index = ZYDIS_REGISTER_NONE;
    operand.mem.scale = 0;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemVRegVReg:
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = convert_reg(arg.reg);
    operand.mem.displacement = 0;
    operand.mem.index = convert_reg(arg.indx);
    operand.mem.scale = 1;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemVRegVRegScale:
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = convert_reg(arg.reg);
    operand.mem.displacement = 0;
    operand.mem.index = convert_reg(arg.indx);
    operand.mem.scale = 1 << arg.scale;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemVReg:
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = convert_reg(arg.reg);
    operand.mem.displacement = 0;
    operand.mem.index = ZYDIS_REGISTER_NONE;
    operand.mem.scale = 0;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemImmVReg:
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = convert_reg(arg.reg);
    operand.mem.displacement = arg.imm;
    operand.mem.index = ZYDIS_REGISTER_NONE;
    operand.mem.scale = 0;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemImm:
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = ZYDIS_REGISTER_NONE;
    operand.mem.displacement = arg.imm;
    operand.mem.index = ZYDIS_REGISTER_NONE;
    operand.mem.scale = 0;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemImmVRegVReg:
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = convert_reg(arg.reg);
    operand.mem.displacement = arg.imm;
    operand.mem.index = convert_reg(arg.indx);
    operand.mem.scale = 1;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
    operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
    operand.mem.base = convert_reg(arg.reg);
    operand.mem.displacement = arg.imm;
    operand.mem.index = convert_reg(arg.indx);
    operand.mem.scale = 1 << arg.scale;
    operand.mem.size = get_size(arg.ty);
    return;
  case fmir::MArgument::ArgumentType::MemImmVRegScale:
    utils::Debug << "impl operand: " << arg << "\n";
    TODO("");
  }
}

#define ZY_ASS(status)                                                         \
  do {                                                                         \
    const ZyanStatus status_047620348 = (status);                              \
    if (!ZYAN_SUCCESS(status_047620348)) {                                     \
      utils::Debug << "Zyan op failed: " << ZYAN_STATUS_CODE(status_047620348) \
                   << " in module:" << ZYAN_STATUS_MODULE(status_047620348)    \
                   << "\n";                                                    \
      TODO("");                                                                \
    }                                                                          \
  } while (0)

size_t emit_instr(fmir::MInstr &instr, u8 *const out_buff, u8 curr_bb_id,
                  TLabelUsageMap &reloc_map) {
  size_t length = 999;
  ZydisEncoderRequest req;
  memset(&req, 0, sizeof(req));
  utils::Debug << "Convert: " << instr << "\n";
  req.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
  req.operand_count = instr.n_args;
  for (auto i = 0; i < req.operand_count; i++) {
    emit_operand(instr.args[i], req.operands[i], reloc_map, out_buff, i);
    utils::Debug << "   With Op:" << req.operands[i] << "\n";
  }

  switch (instr.op) {
  case fmir::Opcode::mov: {
    bool input_is_fp_reg =
        instr.args[1].isReg() &&
        instr.args[1].reg.info.reg_class == fmir::VRegClass::Float &&
        instr.args[1].reg.info.isVecReg();
    bool target_is_fp_reg =
        instr.args[0].isReg() &&
        instr.args[0].reg.info.reg_class == fmir::VRegClass::Float &&
        instr.args[0].reg.info.isVecReg();
    bool target_isfloat64 =
        target_is_fp_reg && instr.args[0].reg.info.reg_size == 8;
    bool target_isfloat32 =
        target_is_fp_reg && instr.args[0].reg.info.reg_size == 4;

    req.mnemonic = ZYDIS_MNEMONIC_MOV;

    if (!input_is_fp_reg && target_isfloat32) {
      req.mnemonic = ZYDIS_MNEMONIC_MOVD;
    } else if (!input_is_fp_reg && target_isfloat64) {
      req.mnemonic = ZYDIS_MNEMONIC_MOVQ;
    } else if (target_isfloat32) {
      req.mnemonic = ZYDIS_MNEMONIC_MOVSS;
    } else if (target_isfloat64) {
      req.mnemonic = ZYDIS_MNEMONIC_MOVSD;
    }
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  }
  case fmir::Opcode::call:
    req.mnemonic = ZYDIS_MNEMONIC_CALL;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::ret: { // do the epilogue
    size_t length2 = 999;
    size_t length3 = 999;
    ZydisEncoderRequest req;
    memset(&req, 0, sizeof(req));
    req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    req.operands[1].type = ZYDIS_OPERAND_TYPE_REGISTER;

    req.mnemonic = ZYDIS_MNEMONIC_MOV;
    req.operand_count = 2;
    req.operands[0].reg.value = ZYDIS_REGISTER_RSP;
    req.operands[1].reg.value = ZYDIS_REGISTER_RBP;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length3));

    req.mnemonic = ZYDIS_MNEMONIC_POP;
    req.operand_count = 1;
    req.operands[0].reg.value = ZYDIS_REGISTER_RBP;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + length3, &length2));

    req.mnemonic = ZYDIS_MNEMONIC_RET;
    req.operand_count = 0;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + length2 + length3,
                                         &length));
    return length + length3 + length2;
  }
  case fmir::Opcode::push:
    req.mnemonic = ZYDIS_MNEMONIC_PUSH;
    assert(req.operand_count == 1);
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::pop:
    req.mnemonic = ZYDIS_MNEMONIC_POP;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::jmp:
    req.mnemonic = ZYDIS_MNEMONIC_JMP;
    req.branch_type = ZYDIS_BRANCH_TYPE_NEAR;
    req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    req.operands[0].imm.s = 0;
    req.operand_count = 1;
    ASSERT(instr.has_bb_ref);
    if (instr.bb_ref == curr_bb_id + 1) {
      return 0;
    }
    reloc_map.insert_bb_ref(instr.bb_ref, out_buff, 0, RelocSection::Text);
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::mul2:
    req.mnemonic = ZYDIS_MNEMONIC_IMUL;
    // TODO: need to check if its fits correclty
    if (req.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
        instr.args[1].imm < 60000) {
      req.operands[2] = req.operands[1];
      req.operands[1] = req.operands[0];
      req.operand_count = 3;
    }
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::idiv:
    req.mnemonic = ZYDIS_MNEMONIC_IDIV;
    req.operand_count = 1;
    req.operands[0] = req.operands[3];
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::add2:
    req.mnemonic = ZYDIS_MNEMONIC_ADD;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::sub2:
    req.mnemonic = ZYDIS_MNEMONIC_SUB;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::land2:
    req.mnemonic = ZYDIS_MNEMONIC_AND;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::lor2:
    req.mnemonic = ZYDIS_MNEMONIC_OR;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::lxor2:
    req.mnemonic = ZYDIS_MNEMONIC_XOR;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::fadd:
    req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                       ? ZYDIS_MNEMONIC_VADDSS
                       : ZYDIS_MNEMONIC_VADDSD;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::fsub:
    req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                       ? ZYDIS_MNEMONIC_VSUBSS
                       : ZYDIS_MNEMONIC_VSUBSD;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::fmul:
    req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                       ? ZYDIS_MNEMONIC_VMULSS
                       : ZYDIS_MNEMONIC_VMULSD;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::fdiv:
    req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                       ? ZYDIS_MNEMONIC_VDIVSS
                       : ZYDIS_MNEMONIC_VDIVSD;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::icmp_slt:
  case fmir::Opcode::icmp_ult:
  case fmir::Opcode::icmp_ne:
  case fmir::Opcode::icmp_sgt:
  case fmir::Opcode::icmp_uge:
  case fmir::Opcode::icmp_ule:
  case fmir::Opcode::icmp_sge:
  case fmir::Opcode::icmp_sle:
  case fmir::Opcode::icmp_ugt:
  case fmir::Opcode::icmp_eq: {
    auto targ = req.operands[0];
    auto a = req.operands[1];
    auto b = req.operands[2];
    req.mnemonic = ZYDIS_MNEMONIC_CMP;
    req.operands[0] = a;
    req.operands[1] = b;
    req.operand_count = 2;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    size_t len2 = 32;
    req.mnemonic = ZYDIS_MNEMONIC_INVALID;

    switch (instr.op) {
    case fmir::Opcode::icmp_slt:
      TODO("");
      req.mnemonic = ZYDIS_MNEMONIC_INVALID;
      break;
    case fmir::Opcode::icmp_ult:
      req.mnemonic = ZYDIS_MNEMONIC_SETB;
      break;
    case fmir::Opcode::icmp_ne:
      TODO("");
      req.mnemonic = ZYDIS_MNEMONIC_INVALID;
      break;
    case fmir::Opcode::icmp_sgt:
      TODO("");
      req.mnemonic = ZYDIS_MNEMONIC_INVALID;
      break;
    case fmir::Opcode::icmp_uge:
      TODO("");
      req.mnemonic = ZYDIS_MNEMONIC_INVALID;
      break;
    case fmir::Opcode::icmp_ule:
      TODO("");
      req.mnemonic = ZYDIS_MNEMONIC_SETBE;
      break;
    case fmir::Opcode::icmp_sge:
      TODO("");
      req.mnemonic = ZYDIS_MNEMONIC_INVALID;
      break;
    case fmir::Opcode::icmp_sle:
      req.mnemonic = ZYDIS_MNEMONIC_SETLE;
      break;
    case fmir::Opcode::icmp_ugt:
      req.mnemonic = ZYDIS_MNEMONIC_SETNBE;
      break;
    case fmir::Opcode::icmp_eq:
      req.mnemonic = ZYDIS_MNEMONIC_SETZ;
      break;
    default:
      UNREACH();
    }
    req.operands[0] = targ;
    req.operand_count = 1;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + length, &len2));
    return length + len2;
  }
  case fmir::Opcode::cjmp_int_sge:
  case fmir::Opcode::cjmp_int_sle:
  case fmir::Opcode::cjmp_int_sgt:
  case fmir::Opcode::cjmp_int_ult:
  case fmir::Opcode::cjmp_int_ugt:
  case fmir::Opcode::cjmp_int_uge:
  case fmir::Opcode::cjmp_int_ne:
  case fmir::Opcode::cjmp_int_eq:
  case fmir::Opcode::cjmp_int_slt:
  case fmir::Opcode::cjmp_int_ule: {
    req.mnemonic = ZYDIS_MNEMONIC_CMP;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    size_t len2 = 32;
    switch (instr.op) {
    case fmir::Opcode::cjmp_int_slt:
      req.mnemonic = ZYDIS_MNEMONIC_JL;
      break;
    case fmir::Opcode::cjmp_int_ule:
      req.mnemonic = ZYDIS_MNEMONIC_JBE;
      break;
    case fmir::Opcode::cjmp_int_sge:
      req.mnemonic = ZYDIS_MNEMONIC_JNL;
      break;
    case fmir::Opcode::cjmp_int_sle:
      req.mnemonic = ZYDIS_MNEMONIC_JLE;
      break;
    case fmir::Opcode::cjmp_int_sgt:
      req.mnemonic = ZYDIS_MNEMONIC_JNLE;
      break;
    case fmir::Opcode::cjmp_int_ult:
      req.mnemonic = ZYDIS_MNEMONIC_JB;
      break;
    case fmir::Opcode::cjmp_int_ugt:
      req.mnemonic = ZYDIS_MNEMONIC_JNBE;
      break;
    case fmir::Opcode::cjmp_int_uge:
      req.mnemonic = ZYDIS_MNEMONIC_JNB;
      break;
    case fmir::Opcode::cjmp_int_ne:
      req.mnemonic = ZYDIS_MNEMONIC_JNZ;
      break;
    case fmir::Opcode::cjmp_int_eq:
      req.mnemonic = ZYDIS_MNEMONIC_JZ;
      break;
    default:
      UNREACH();
    }
    req.branch_type = ZYDIS_BRANCH_TYPE_NEAR;
    req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    req.operands[0].imm.s = 0;
    req.operand_count = 1;
    ASSERT(instr.has_bb_ref);
    reloc_map.insert_bb_ref(instr.bb_ref, out_buff + length, 0,
                            RelocSection::Text);
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + length, &len2));
    return length + len2;
  }
  case fmir::Opcode::cjmp: {
    size_t len2 = 999;
    req.mnemonic = ZYDIS_MNEMONIC_TEST;
    req.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    req.operands[1].imm.s = 0;
    req.operand_count = 2;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    ASSERT(instr.has_bb_ref);
    req.mnemonic = ZYDIS_MNEMONIC_JNZ;
    req.branch_type = ZYDIS_BRANCH_TYPE_NEAR;
    req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    req.operands[0].imm.s = 0;
    req.operand_count = 1;
    reloc_map.insert_bb_ref(instr.bb_ref, out_buff + length, 0,
                            RelocSection::Text);
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + length, &len2));
    return length + len2;
  }

  case fmir::Opcode::cmov: {
    auto targ = req.operands[0];
    auto cond = req.operands[1];
    auto value = req.operands[2];
    req.mnemonic = ZYDIS_MNEMONIC_TEST;
    req.operands[0] = cond;
    req.operands[1] = cond;
    req.operand_count = 2;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    size_t len2 = 32;
    req.mnemonic = ZYDIS_MNEMONIC_CMOVZ;
    req.operands[0] = targ;
    req.operands[1] = value;
    req.operand_count = 2;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + length, &len2));
    return length + len2;
  }
  case fmir::Opcode::lea:
    req.mnemonic = ZYDIS_MNEMONIC_LEA;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::sar2:
    req.mnemonic = ZYDIS_MNEMONIC_SAR;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::mov_sx:
    if (instr.args[0].ty == instr.args[1].ty) {
      req.mnemonic = ZYDIS_MNEMONIC_MOV;
    } else if (4 == get_size(instr.args[1].ty)) {
      req.mnemonic = ZYDIS_MNEMONIC_MOVSXD;
    } else {
      req.mnemonic = ZYDIS_MNEMONIC_MOVSX;
    }
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::mov_zx:
    if (instr.args[0].ty == instr.args[1].ty) {
      req.mnemonic = ZYDIS_MNEMONIC_MOV;
    } else if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int64 &&
               instr.args[1].ty == fmir::Type::Int32) {
      req.mnemonic = ZYDIS_MNEMONIC_MOV;
      req.operands[1].reg.value = reg_with_size(instr.args[1].reg, 8);
      // auto increased_reg = instr.args[1].reg;
      // increased_reg.info.reg_size = 8;
      // req.operands[1].reg.value = convert_reg(increased_reg);
    } else {
      req.mnemonic = ZYDIS_MNEMONIC_MOVZX;
    }
    // utils::Debug << instr << "\n";
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::itrunc:
    req.mnemonic = ZYDIS_MNEMONIC_MOV;
    if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int64) {
      req.operands[1].reg.value = reg_with_size(instr.args[1].reg, 8);
    } else if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int32) {
      req.operands[1].reg.value = reg_with_size(instr.args[1].reg, 4);
    } else if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int16) {
      req.operands[1].reg.value = reg_with_size(instr.args[1].reg, 2);
    } else if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int8) {
      req.operands[1].reg.value = reg_with_size(instr.args[1].reg, 1);
    }
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
    return length;
  case fmir::Opcode::shl2:
  case fmir::Opcode::shr2:
  case fmir::Opcode::ffmadd132:
  case fmir::Opcode::ffmadd213:
  case fmir::Opcode::ffmadd231:
  case fmir::Opcode::fxor:
  case fmir::Opcode::SI2FL:
  case fmir::Opcode::UI2FL:
  case fmir::Opcode::FL2SI:
  case fmir::Opcode::FL2UI:
  case fmir::Opcode::fcmp_oeq:
  case fmir::Opcode::fcmp_ogt:
  case fmir::Opcode::fcmp_oge:
  case fmir::Opcode::fcmp_olt:
  case fmir::Opcode::fcmp_ole:
  case fmir::Opcode::fcmp_one:
  case fmir::Opcode::fcmp_ord:
  case fmir::Opcode::fcmp_uno:
  case fmir::Opcode::fcmp_ueq:
  case fmir::Opcode::fcmp_ugt:
  case fmir::Opcode::fcmp_uge:
  case fmir::Opcode::fcmp_ult:
  case fmir::Opcode::fcmp_ule:
  case fmir::Opcode::fcmp_une:
  case fmir::Opcode::cjmp_flt_oeq:
  case fmir::Opcode::cjmp_flt_ogt:
  case fmir::Opcode::cjmp_flt_oge:
  case fmir::Opcode::cjmp_flt_olt:
  case fmir::Opcode::cjmp_flt_ole:
  case fmir::Opcode::cjmp_flt_one:
  case fmir::Opcode::cjmp_flt_ord:
  case fmir::Opcode::cjmp_flt_uno:
  case fmir::Opcode::cjmp_flt_ueq:
  case fmir::Opcode::cjmp_flt_ugt:
  case fmir::Opcode::cjmp_flt_uge:
  case fmir::Opcode::cjmp_flt_ult:
  case fmir::Opcode::cjmp_flt_ule:
  case fmir::Opcode::cjmp_flt_une:
  case fmir::Opcode::arg_setup:
  case fmir::Opcode::invoke:
    utils::Debug << " impl: " << instr << "\n";
    UNREACH();
  }
}

void diss_print(u8 *buff) {
  ZydisDisassembledInstruction instruction;
  ZY_ASS(ZydisDisassembleIntel(
      /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
      /* runtime_address: */ 0,
      /* buffer:          */ buff,
      /* length:          */ 999,
      /* instruction:     */ &instruction));
  utils::Debug << " DEBUG: " << instruction.text << "\n";
}

struct OpData {
  u8 *op_addr;
  u8 op_off;
  i64 op_val;
};

// returns the address of the operand + the offset till end of instruction
OpData get_op_addr(u8 *buff, u8 op_num) {
  ZydisDecoder decoder;
  // TOOD: maybe dont do this inside of here so it can be done only once
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  ZydisDecodedInstruction instruction;
  memset(&instruction, 0, sizeof(ZydisDecodedInstruction));
  ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
  memset(operands, 0, sizeof(ZydisDecodedOperand));

  // diss_print(buff_instr);
  ZY_ASS(ZydisDecoderDecodeFull(&decoder, buff, 999, &instruction, operands));

  assert(op_num < instruction.operand_count_visible);
  assert(operands[op_num].type == ZYDIS_OPERAND_TYPE_IMMEDIATE ||
         operands[op_num].type == ZYDIS_OPERAND_TYPE_MEMORY);

  bool is_imm = operands[op_num].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
  ZydisInstructionSegments segments;
  ZY_ASS(ZydisGetInstructionSegments(&instruction, &segments));

  u8 op_off = 0;

  for (auto i = 0; i < segments.count; i++) {
    auto &segment = segments.segments[i];
    if (!is_imm && segment.type == ZYDIS_INSTR_SEGMENT_DISPLACEMENT) {
      op_off = segment.offset;
    }
    if (is_imm && segment.type == ZYDIS_INSTR_SEGMENT_IMMEDIATE) {
      op_off = segment.offset;
    }
  }

  auto old_val = is_imm ? operands[op_num].imm.value.s : operands[op_num].mem.disp.value;
  return {buff + op_off, (u8)(instruction.length - op_off),
          old_val};
}

void reloc_bbs(TLabelUsageMap &reloc_map, u8 *buff_start) {
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

  for (auto &[bb_id, bb_data] : reloc_map.bb_map) {
    auto bb_loc = bb_data.def_loc;
    // utils::Debug << "\nbb_loc: " << bb_loc << "\n";

    for (auto &usage : bb_data.usage_loc) {
      auto *buff_instr = usage.usage_instr;
      ZydisDecodedInstruction instruction;
      memset(&instruction, 0, sizeof(ZydisDecodedInstruction));
      ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
      memset(operands, 0, sizeof(ZydisDecodedOperand));

      // diss_print(buff_instr);
      ZY_ASS(ZydisDecoderDecodeFull(&decoder, buff_instr, 999, &instruction,
                                    operands));

      assert(usage.operand_num < instruction.operand_count_visible);
      assert(operands[usage.operand_num].type == ZYDIS_OPERAND_TYPE_IMMEDIATE);

      ZydisEncoderRequest req;
      ZydisEncoderDecodedInstructionToEncoderRequest(
          &instruction, operands, instruction.operand_count_visible, &req);
      req.operands[usage.operand_num].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
      req.operands[usage.operand_num].imm.u = bb_loc;

      size_t len_out = 999;
      ZY_ASS(ZydisEncoderEncodeInstructionAbsolute(&req, buff_instr, &len_out,
                                                   buff_instr - buff_start));
      ASSERT(len_out == instruction.length);
      // diss_print(buff_instr);
    }
  }
}

u8 *assemble(std::span<const fmir::MFunc> funcs, u8 *const out_buff,
             TLabelUsageMap &reloc_map) {

  u8 *curr_loc = out_buff;
  for (const auto &func : funcs) {
    { // make sure were aligned
      auto offset_from_section = (curr_loc - out_buff);
      auto align_offset = offset_from_section % 0x10;
      if (align_offset != 0) {
        auto align_correction = 0x10 - align_offset;
        ZydisEncoderNopFill(curr_loc, align_correction);
        curr_loc += align_correction;
      }
    }

    u8 *func_start = curr_loc;
    reloc_map.label_map[func.name].def_loc = curr_loc - out_buff;
    reloc_map.label_map[func.name].section = RelocSection::Text;
    reloc_map.label_map[func.name].kind = RelocKind::Func;

    // prologue
    {
      size_t length = 999;
      ZydisEncoderRequest req;
      memset(&req, 0, sizeof(req));
      req.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
      req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
      req.operands[1].type = ZYDIS_OPERAND_TYPE_REGISTER;

      req.mnemonic = ZYDIS_MNEMONIC_PUSH;
      req.operand_count = 1;
      req.operands[0].reg.value = ZYDIS_REGISTER_RBP;
      ZY_ASS(ZydisEncoderEncodeInstruction(&req, curr_loc, &length));
      curr_loc += length;
      length = 999;
      req.mnemonic = ZYDIS_MNEMONIC_MOV;
      req.operand_count = 2;
      req.operands[0].reg.value = ZYDIS_REGISTER_RBP;
      req.operands[1].reg.value = ZYDIS_REGISTER_RSP;
      ZY_ASS(ZydisEncoderEncodeInstruction(&req, curr_loc, &length));
      curr_loc += length;
    }

    u64 bb_id = 0;
    for (const auto &bb : func.bbs) {
      reloc_map.bb_map[bb_id].def_loc = curr_loc - out_buff;
      reloc_map.bb_map[bb_id].section = RelocSection::Text;
      reloc_map.bb_map[bb_id].kind = RelocKind::BB;

      for (auto instr : bb.instrs) {
        curr_loc += emit_instr(instr, curr_loc, bb_id, reloc_map);
      }
      bb_id++;
    }
    u8 *func_end = curr_loc;
    reloc_map.label_map[func.name].size = func_end - func_start;

    reloc_bbs(reloc_map, out_buff);
    reloc_map.bb_map.clear();
  }
  return curr_loc;
}

void generate_obj_file(TLabelUsageMap &label_usage_map, u8 *start_txt,
                       u8 *end_txt, std::span<const IRString> decls,
                       std::span<const fmir::Global> globals) {
  using namespace ELFIO;

  elfio writer;
  {
    writer.create(ELFCLASS64, ELFDATA2LSB);
    writer.set_os_abi(ELFOSABI_LINUX);
    writer.set_type(ET_REL);
    writer.set_machine(EM_X86_64);
  }

  // code section
  section *text_sec = writer.sections.add(".text");
  {
    text_sec->set_type(SHT_PROGBITS);
    text_sec->set_flags(SHF_ALLOC | SHF_EXECINSTR);
    text_sec->set_addr_align(0x10);
    text_sec->set_data((const char *)start_txt,
                       (size_t)end_txt - (size_t)start_txt);
  }

  for (auto &decl : decls) {
    label_usage_map.label_map[decl].def_loc = 0;
    label_usage_map.label_map[decl].kind = RelocKind::Func;
    label_usage_map.label_map[decl].section = RelocSection::Extern;
    label_usage_map.label_map[decl].size = 0;
  }

  // Create string table section
  section *str_sec = writer.sections.add(".strtab");
  str_sec->set_type(SHT_STRTAB);
  string_section_accessor stra(str_sec);

  section *sym_sec = writer.sections.add(".symtab");
  {
    sym_sec->set_type(SHT_SYMTAB);
    sym_sec->set_info(1);
    sym_sec->set_addr_align(0x4);
    sym_sec->set_entry_size(writer.get_default_entry_size(SHT_SYMTAB));
    sym_sec->set_link(str_sec->get_index());
  }
  symbol_section_accessor syma(writer, sym_sec);

  section *text_rel_sec = writer.sections.add(".rela.text");
  {
    text_rel_sec->set_type(SHT_RELA);
    text_rel_sec->set_info(text_sec->get_index());
    text_rel_sec->set_addr_align(0x4);
    text_rel_sec->set_entry_size(writer.get_default_entry_size(SHT_RELA));
    text_rel_sec->set_link(sym_sec->get_index());
  }
  relocation_section_accessor text_rela(writer, text_rel_sec);

  section *data_sec = writer.sections.add(".data");
  u8 *start_data = nullptr;
  {
    data_sec->set_type(SHT_PROGBITS);
    data_sec->set_flags(SHF_ALLOC | SHF_WRITE);
    data_sec->set_addr_align(0x4);
    size_t global_data_size = 0;
    for (auto global : globals) {
      global_data_size += global.data.size();
    }
    start_data = utils::TempAlloc<u8>{}.allocate(global_data_size);
    auto *curr_data_ptr = start_data;
    // TODO: alignment
    data_sec->set_address(0x1000);
    for (auto global : globals) {
      label_usage_map.label_map[global.name].def_loc =
          curr_data_ptr - start_data;
      label_usage_map.label_map[global.name].kind = RelocKind::Data;
      label_usage_map.label_map[global.name].section = RelocSection::Data;
      label_usage_map.label_map[global.name].size = global.data.size();
      memcpy(curr_data_ptr, global.data.data(), global.data.size());
      curr_data_ptr += global.data.size();
    }
    data_sec->set_data((char *)start_data, global_data_size);
  }

  section *data_rel_sec = writer.sections.add(".rela.data");
  {
    data_rel_sec->set_type(SHT_RELA);
    data_rel_sec->set_info(data_sec->get_index());
    data_rel_sec->set_addr_align(0x4);
    data_rel_sec->set_entry_size(writer.get_default_entry_size(SHT_RELA));
    data_rel_sec->set_link(sym_sec->get_index());
  }
  relocation_section_accessor data_rela(writer, data_rel_sec);

  for (auto [label_name, label_data] : label_usage_map.label_map) {
    utils::Debug << label_name.c_str() << "\n";
    ASSERT(label_data.section != RelocSection::INVALID);

    Elf_Half sec_indx = 0;
    Elf_Word symbol_type = STT_FUNC;
    Elf_Word symbol_binding = STB_GLOBAL;
    switch (label_data.section) {
    case RelocSection::INVALID:
      UNREACH();
    case RelocSection::Data:
      sec_indx = data_sec->get_index();
      break;
    case RelocSection::Text:
      sec_indx = text_sec->get_index();
      break;
    case RelocSection::Extern:
      sec_indx = 0;
      break;
    }
    switch (label_data.kind) {
    case RelocKind::INVALID:
    case RelocKind::BB:
      UNREACH();
    case RelocKind::Func:
      symbol_type = STT_FUNC;
      break;
    case RelocKind::Data:
      symbol_type = STT_OBJECT;
      break;
    }

    auto symbol = syma.add_symbol(stra, label_name.c_str(), label_data.def_loc,
                                  label_data.size, symbol_binding, symbol_type,
                                  0, sec_indx);

    for (auto loc : label_data.usage_loc) {
      ASSERT(loc.usage_section != RelocSection::INVALID);
      auto data = get_op_addr(loc.usage_instr, loc.operand_num);
      switch (loc.usage_section) {
      case RelocSection::INVALID:
      case RelocSection::Extern:
        UNREACH();
      case RelocSection::Data:
        TODO("impl");
        // data_rela.add_entry(op_addr - start_data, symbol,
        //                     (unsigned char)R_X86_64_PLT32, -4);
        // break;
      case RelocSection::Text:
        switch (label_data.section) {
        case RelocSection::INVALID:
          UNREACH();
        case RelocSection::Data:
          text_rela.add_entry(data.op_addr - start_txt, symbol,
                              (unsigned char)R_X86_64_PC32,
                              -data.op_off + data.op_val);
          break;
        case RelocSection::Extern:
        case RelocSection::Text:
          text_rela.add_entry(data.op_addr - start_txt, symbol,
                              (unsigned char)R_X86_64_PLT32,
                              -data.op_off + data.op_val);
          break;
        }
        break;
      }
    }
  }

  section *note_gnu_stack_sec = writer.sections.add(".note.GNU-stack");
  { // section .note.GNU-stack noalloc noexec nowrite progbits
    note_gnu_stack_sec->set_type(SHT_PROGBITS);
  }

  section *note_sec = writer.sections.add(".note");
  {
    note_sec->set_type(SHT_NOTE);
    note_section_accessor note_writer(writer, note_sec);
    note_writer.add_note(0x01, "Created By Foptim", nullptr, 0);
  }

  syma.arrange_local_symbols([&](Elf_Xword first, Elf_Xword second) {
    Elf64_Addr off;
    Elf_Word symbol;
    unsigned char type;
    Elf_Sxword addend;
    if (!text_rela.get_entry(first, off, symbol, type, addend)) {
      text_rela.swap_symbols(first, second);
    } else if (!data_rela.get_entry(first, off, symbol, type, addend)) {
      data_rela.swap_symbols(first, second);
    }
  });

  ASSERT(writer.save(utils::out_file_path));
}

void run(std::span<const fmir::MFunc> funcs, std::span<const IRString> decls,
         std::span<const fmir::Global> globals) {
  size_t n_instrs = 0;
  for (const auto &func : funcs) {
    for (const auto &bb : func.bbs) {
      for (const auto &_ : bb.instrs) {
        n_instrs++;
      }
    }
  }

  auto *output_buffer = utils::TempAlloc<u8>{}.allocate(n_instrs * 16);
  memset(output_buffer, 0xFF, n_instrs * 16);
  TLabelUsageMap label_usages;
  auto *end_buff_ptr = assemble(funcs, output_buffer, label_usages);

  utils::Debug << " Needs " << label_usages.label_map.size()
               << " Relocations\n";
  utils::Debug << " Generated " << end_buff_ptr - output_buffer << " Bytes\n";

  generate_obj_file(label_usages, output_buffer, end_buff_ptr, decls, globals);

  {
    ZyanU64 runtime_address = 0;
    ZyanUSize offset = 0;
    ZydisDisassembledInstruction instruction;
    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
        /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
        /* runtime_address: */ runtime_address,
        /* buffer:          */ output_buffer + offset,
        /* length:          */ (end_buff_ptr - output_buffer) - offset,
        /* instruction:     */ &instruction))) {
      utils::Debug << utils::Hex(runtime_address) << ": " << instruction.text
                   << "\n";
      offset += instruction.info.length;
      runtime_address += instruction.info.length;
    }
  }
}

} // namespace foptim::codegen
