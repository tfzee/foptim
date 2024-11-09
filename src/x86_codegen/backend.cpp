#include "backend.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "utils/logging.hpp"
#include "utils/parameters.hpp"
#include "utils/todo.hpp"
#include <asmjit/core/emitter.h>
#include <asmjit/core/func.h>
#include <asmjit/core/inst.h>
#include <asmjit/core/logger.h>
#include <asmjit/core/operand.h>
#include <asmjit/core/type.h>
#include <asmjit/x86.h>
#include <asmjit/x86/x86builder.h>
#include <asmjit/x86/x86globals.h>
#include <asmjit/x86/x86operand.h>
#include <cstdint>
#include <fstream>
#include <span>

namespace foptim::codegen {

using namespace asmjit;
using namespace asmjit::x86;

using Reg2OpMap = TMap<fmir::VReg, Reg>;

static constexpr TypeId convert_type(fmir::Type type) {
  switch (type) {
  case fmir::Type::Float32:
    return TypeId::kFloat32;
  case fmir::Type::Float64:
    return TypeId::kFloat64;
  case fmir::Type::Int8:
    return TypeId::kInt8;
  case fmir::Type::Int16:
    return TypeId::kInt16;
  case fmir::Type::Int32:
    return TypeId::kInt32;
  case fmir::Type::Int64:
    return TypeId::kInt64;
  case fmir::Type::INVALID:
  }
  ASSERT(false);
  std::abort();
}

auto convert_func_signature(const std::span<fmir::Type> arg_tys, bool void_ret,
                            const fmir::Type ret_ty) {
  auto res = FuncSignatureBuilder();
  for (auto arg : arg_tys) {
    res.addArg(convert_type(arg));
  }
  if (!void_ret) {
    res.setRet(convert_type(ret_ty));
  }
  return res;
}

Reg get_reg_sized(const Reg *regs, u32 size) {
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

Reg convert_reg(Compiler & /*unused*/, Reg2OpMap & /*unused*/, fmir::VReg reg) {

  static_assert(1 == (u16)fmir::Type::Int8);
  static_assert(4 == (u16)fmir::Type::Int64);

  constexpr Reg a_regs[] = {al, ax, eax, rax};
  constexpr Reg b_regs[] = {bl, bx, ebx, rbx};
  constexpr Reg c_regs[] = {cl, cx, ecx, rcx};
  constexpr Reg d_regs[] = {dl, dx, edx, rdx};
  constexpr Reg s_regs[] = {sil, si, esi, rsi};
  constexpr Reg sp_regs[] = {spl, sp, esp, rsp};
  constexpr Reg bp_regs[] = {bpl, bp, ebp, rbp};
  constexpr Reg r8_regs[] = {r8b, r8w, r8d, r8};
  constexpr Reg r9_regs[] = {r9b, r9w, r9d, r9};
  constexpr Reg r10_regs[] = {r10b, r10w, r10d, r10};
  constexpr Reg r11_regs[] = {r11b, r11w, r11d, r11};
  constexpr Reg r12_regs[] = {r12b, r12w, r12d, r12};
  constexpr Reg r13_regs[] = {r13b, r13w, r13d, r13};
  constexpr Reg r14_regs[] = {r14b, r14w, r14d, r14};
  constexpr Reg r15_regs[] = {r15b, r15w, r15d, r15};

  switch (reg.info.ty) {
  case fmir::VRegType::Virtual:
    ASSERT(false);
    std::abort();
  case fmir::VRegType::A:
    return get_reg_sized(a_regs, reg.info.reg_size);
  case fmir::VRegType::B:
    return get_reg_sized(b_regs, reg.info.reg_size);
  case fmir::VRegType::C:
    return get_reg_sized(c_regs, reg.info.reg_size);
  case fmir::VRegType::D:
    return get_reg_sized(d_regs, reg.info.reg_size);
  case fmir::VRegType::S:
    return get_reg_sized(s_regs, reg.info.reg_size);
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
    return xmm0;
  case fmir::VRegType::mm1:
    return xmm1;
  case fmir::VRegType::mm2:
    return xmm2;
  case fmir::VRegType::mm3:
    return xmm3;
  case fmir::VRegType::mm4:
    return xmm4;
  case fmir::VRegType::mm5:
    return xmm5;
  case fmir::VRegType::mm6:
    return xmm6;
  case fmir::VRegType::mm7:
    return xmm7;
  case fmir::VRegType::mm8:
    return xmm8;
  case fmir::VRegType::mm9:
    return xmm9;
  case fmir::VRegType::mm10:
    return xmm10;
  case fmir::VRegType::mm11:
    return xmm11;
  case fmir::VRegType::mm12:
    return xmm12;
  case fmir::VRegType::mm13:
    return xmm13;
  case fmir::VRegType::mm14:
    return xmm14;
  case fmir::VRegType::mm15:
    return xmm15;
  case fmir::VRegType::N_REGS:
    TODO("UNREACH");
  }
}

Imm convert_imm(Compiler & /*unused*/, fmir::MArgument arg, fmir::Type ty) {
  // utils::Debug << "     Converting Imm: " << imm << "\n";
  switch (ty) {
  case fmir::Type::Float32: {
    auto val = (f32)arg.immf;
    return {*reinterpret_cast<u32 *>(&val)};
    // return {(f32)arg.immf};
  }
  case fmir::Type::Float64:
    return {*reinterpret_cast<u64 *>(&arg.immf)};
    // return {arg.immf};
  case fmir::Type::Int8:
  case fmir::Type::Int16:
  case fmir::Type::Int32:
  case fmir::Type::Int64:
    return arg.imm;
  case fmir::Type::INVALID:
    TODO("Invalid type in MIR");
    break;
  }
  std::abort();
}

Operand convert_operand(Compiler &cc, Reg2OpMap &reg_to_op,
                        fmir::MArgument &arg) {
  switch (arg.type) {
  case fmir::MArgument::ArgumentType::Imm:
    return convert_imm(cc, arg, arg.ty);
  case fmir::MArgument::ArgumentType::VReg:
    return convert_reg(cc, reg_to_op, arg.reg);
  case fmir::MArgument::ArgumentType::Label: {
    Label label = cc.labelByName(arg.label.c_str());
    if (!cc.isLabelValid(label)) {
      label = cc.newExternalLabel(arg.label.c_str());
    }
    return label;
  }
  case fmir::MArgument::ArgumentType::MemImmLabel: {
    auto label = cc.labelByName(arg.label.c_str());
    if (!cc.isLabelValid(label)) {
      label = cc.newExternalLabel(arg.label.c_str());
    }
    auto res = Mem(label, arg.imm);
    res.setSize(get_size(arg.ty));
    return res;
  }
  case fmir::MArgument::ArgumentType::MemLabel: {
    auto label = cc.labelByName(arg.label.c_str());
    if (!cc.isLabelValid(label)) {
      label = cc.newExternalLabel(arg.label.c_str());
    }
    auto res = Mem(label, 0);
    res.setSize(get_size(arg.ty));
    return res;
  }
  case fmir::MArgument::ArgumentType::MemVReg: {
    auto res = Mem(convert_reg(cc, reg_to_op, arg.reg), 0);
    res.setSize(get_size(arg.ty));
    return res;
  }
  case fmir::MArgument::ArgumentType::MemImmVReg: {
    auto res = Mem(convert_reg(cc, reg_to_op, arg.reg), (int32_t)arg.imm);
    res.setSize(get_size(arg.ty));
    return res;
  }
  case fmir::MArgument::ArgumentType::MemVRegVReg: {
    auto res = Mem(convert_reg(cc, reg_to_op, arg.reg),
                   convert_reg(cc, reg_to_op, arg.indx), 0, 0);
    res.setSize(get_size(arg.ty));
    return res;
  }
  case fmir::MArgument::ArgumentType::MemImm:
  case fmir::MArgument::ArgumentType::MemImmVRegVReg: {
    auto res = Mem(convert_reg(cc, reg_to_op, arg.reg),
                   convert_reg(cc, reg_to_op, arg.indx), 0, (int32_t)arg.imm);
    res.setSize(get_size(arg.ty));
    return res;
  }
  case fmir::MArgument::ArgumentType::MemVRegVRegScale: {
    auto res = Mem(convert_reg(cc, reg_to_op, arg.reg),
                   convert_reg(cc, reg_to_op, arg.indx), (int32_t)arg.scale, 0);
    res.setSize(get_size(arg.ty));
    return res;
  }
  case fmir::MArgument::ArgumentType::MemImmVRegScale:
    TODO("impl");
    break;
  case fmir::MArgument::ArgumentType::MemImmVRegVRegScale: {
    auto res = Mem(convert_reg(cc, reg_to_op, arg.reg),
                   convert_reg(cc, reg_to_op, arg.indx), (int32_t)arg.scale,
                   (int32_t)arg.imm);
    res.setSize(get_size(arg.ty));
    return res;
  }
  }
  std::abort();
}

void emit_instr(fmir::MInstr &instr, const std::span<Label> &bb_labels,
                Reg2OpMap &reg_to_op, Compiler &cc) {
  (void)bb_labels;
  (void)reg_to_op;
  (void)cc;
  // utils::Debug << "Emitting Instr: " << instr << "\n";
  switch (instr.op) {
  case fmir::Opcode::arg_setup:
  case fmir::Opcode::invoke: {
    ASSERT_M(false, "Invalid instr");
    return;
  }
  case fmir::Opcode::mov_zx: {
    ASSERT(instr.n_args == 2);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[1]);
    if (instr.args[1].ty == instr.args[0].ty) {
      cc.emit(Inst::kIdMov, o0, o1);
    } else if (instr.args[0].ty == fmir::Type::Int64 && o1.isReg()) {
      cc.emit(Inst::kIdMov, o0, o1.as<Gp>().r64());
    } else {
      cc.emit(Inst::kIdMovzx, o0, o1);
    }
    return;
  }
  case fmir::Opcode::mov_sx: {
    ASSERT(instr.n_args == 2);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[1]);
    if (instr.args[0].ty == instr.args[1].ty) {
      cc.emit(Inst::kIdMov, o0, o1);
    } else if (o1.size() == 4) {
      cc.emit(Inst::kIdMovsxd, o0, o1);
    } else {
      cc.emit(Inst::kIdMovsx, o0, o1);
    }
    return;
  }
  case fmir::Opcode::mov: {
    ASSERT(instr.n_args == 2);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[1]);

    u32 o0_size = get_size(instr.args[0].ty);

    if (instr.args[1].ty != fmir::Type::Float32 &&
        instr.args[0].ty == fmir::Type::Float32) {
      cc.emit(Inst::kIdMovd, o0, o1);
    } else if (instr.args[1].ty != fmir::Type::Float64 &&
               instr.args[0].ty == fmir::Type::Float64) {
      cc.emit(Inst::kIdMovq, o0, o1);
    } else if (instr.args[0].ty == fmir::Type::Float32) {
      cc.emit(Inst::kIdMovss, o0, o1);
    } else if (instr.args[0].ty == fmir::Type::Float64) {
      cc.emit(Inst::kIdMovsd, o0, o1);
    } else if (o0_size == 8 && o1.isReg()) {
      cc.emit(Inst::kIdMov, o0, o1.as<Gp>().r64());
    } else {
      cc.emit(Inst::kIdMov, o0, o1);
    }
    return;
  }
  case fmir::Opcode::lea: {
    auto target = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[1]);
    cc.emit(Inst::kIdLea, target, o0);
    return;
  }
  case fmir::Opcode::add: {
    ASSERT(instr.n_args == 3);
    auto target = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[1]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[2]);
    if (target == o0) {
      cc.emit(Inst::kIdAdd, target, o1);
    } else if (target == o1) {
      cc.emit(Inst::kIdAdd, target, o0);
    } else {
      // we move o1 since its mostlikely to be a constant
      cc.emit(Inst::kIdMov, target, o1);
      // if (o0.isImm() && instr.args[1].ty == fmir::Type::Int64) {
      //   // we cant add a 64 bit imm so we also need to move the sec arg
      //   cc.emit(Inst::kIdMov, helper, o0);
      //   cc.emit(Inst::kIdAdd, target, helper);
      // } else {
      cc.emit(Inst::kIdAdd, target, o0);
      // }
    }
    return;
  }
  case fmir::Opcode::sub: {
    ASSERT(instr.n_args == 3);
    auto target = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[1]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[2]);
    ASSERT(target != o1);
    if (target == o0) {
      cc.emit(Inst::kIdSub, target, o1);
    } else {
      cc.emit(Inst::kIdMov, target, o0);
      cc.emit(Inst::kIdSub, target, o1);
    }
    return;
  }
  case fmir::Opcode::fadd: {
    ASSERT(instr.n_args == 3);
    auto target = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[1]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[2]);

    ASSERT(target != o1);
    if (instr.args[0].ty == fmir::Type::Float32) {
      cc.emit(Inst::kIdMovss, target, o0);
      cc.emit(Inst::kIdAddss, target, o1);
    } else if (instr.args[0].ty == fmir::Type::Float64) {
      cc.emit(Inst::kIdMovsd, target, o0);
      cc.emit(Inst::kIdAddsd, target, o1);
    } else {
      TODO("UNREACH");
    }
    return;
  }
  case fmir::Opcode::fsub: {
    ASSERT(instr.n_args == 3);
    auto target = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[1]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[2]);
    TODO("IMPL");
    cc.emit(Inst::kIdSubss, target, o0, o1);
    return;
  }
  case fmir::Opcode::fmul: {
    ASSERT(instr.n_args == 3);
    auto target = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[1]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[2]);
    TODO("IMPL");
    cc.emit(Inst::kIdMulss, target, o0, o1);
    return;
  }
  case fmir::Opcode::idiv: {
    ASSERT(instr.n_args == 4);
    auto div_target = convert_operand(cc, reg_to_op, instr.args[0]);
    auto rem_target = convert_operand(cc, reg_to_op, instr.args[1]);
    ASSERT(div_target.isPhysReg());
    ASSERT(rem_target.isPhysReg());
    ASSERT(div_target == rax || div_target == eax);
    ASSERT(rem_target == rdx || rem_target == edx);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[2]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[3]);
    ASSERT(o0.isPhysReg());
    ASSERT(o0 == rax || o0 == eax || o0 == ax);
    ASSERT(!o1.isPhysReg() || (o1 != rdx && o1 != edx));
    if (o0 == rax) {
      // need to sign extend rax into rdx
      cc.emit(Inst::kIdCqo);
    } else if (o0 == eax) {
      // need to sign extend eax into edx
      cc.emit(Inst::kIdCdq);
    } else if (o0 == ax) {
      // need to sign extend ax into dx
      cc.emit(Inst::kIdCwd);
    }
    // and then div by o1
    cc.emit(Inst::kIdIdiv, o1);
    return;
  }
  case fmir::Opcode::mul: {
    ASSERT(instr.n_args == 3);
    auto target = convert_operand(cc, reg_to_op, instr.args[0]);
    auto o0 = convert_operand(cc, reg_to_op, instr.args[1]);
    auto o1 = convert_operand(cc, reg_to_op, instr.args[2]);
    if (!o0.isImm() && o1.isImm()) {
      cc.emit(Inst::kIdImul, target, o0, o1);
    } else {
      if (target == o0) {
        cc.emit(Inst::kIdImul, target, o1);
      } else if (target == o1) {
        cc.emit(Inst::kIdImul, target, o0);
      } else {
        cc.emit(Inst::kIdMov, target, o0);
        cc.emit(Inst::kIdImul, target, o1);
      }
    }
    return;
  }
  case fmir::Opcode::push: {
    auto val = convert_operand(cc, reg_to_op, instr.args[0]);
    // utils::Debug << instr << "\n";
    // utils::Debug << instr.args[0] << "\n";

    // if (instr.args[0].ty == fmir::Type::Float32 || instr.args[0].ty ==
    // fmir::Type::Float64) {
    //   cc.emit(Inst::kIdPush, val);
    //   return;
    // }
    if (val.size() < 64 && val.isReg()) {
      cc.emit(Inst::kIdPush, val.as<Gp>().r64());
      return;
    }
    cc.emit(Inst::kIdPush, val);
    return;
  }
  case fmir::Opcode::pop: {
    auto val = convert_operand(cc, reg_to_op, instr.args[0]);
    if (val.size() < 64 && val.isReg()) {
      cc.emit(Inst::kIdPop, val.as<Gp>().r64());
      return;
    }
    cc.emit(Inst::kIdPop, val);
    return;
  }
  case fmir::Opcode::call: {
    auto target = convert_operand(cc, reg_to_op, instr.args[0]);
    ASSERT(target.isLabel());
    auto label = target.as<Label>();
    if (!label.isValid()) {
      utils::Debug << instr.args[0].label.c_str() << "\n";
      ASSERT(false);
      std::abort();
    }
    // InvokeNode *node;
    // cc.invoke(&node, label, FuncSignatureBuilder{});
    cc.emit(Inst::kIdCall, target);
    return;
  }
  case fmir::Opcode::jmp: {
    cc.emit(Inst::kIdJmp, bb_labels[instr.bb_ref]);
    return;
  }
  case fmir::Opcode::cjmp: {
    // auto cond = convert_operand(cc, reg_to_op, instr.args[0]);
    // TODO: verify that its perior??
    if (instr.args[0].isImm()) {
      auto value = instr.args[0].imm;
      if (value == 0) {
        // emit nothing
      } else {
        cc.emit(Inst::kIdJmp, bb_labels[instr.bb_ref]);
      }
    } else {
      auto a = convert_operand(cc, reg_to_op, instr.args[0]);
      if (a.isReg()) {
        cc.emit(Inst::kIdCmp, a.as<Gp>().r64(), 0);
      } else {
        cc.emit(Inst::kIdCmp, a, 0);
      }
      cc.emit(Inst::kIdJne, bb_labels[instr.bb_ref]);
    }
    return;
  }
  case fmir::Opcode::cjmp_int_ne:
  case fmir::Opcode::cjmp_int_eq:
  case fmir::Opcode::cjmp_int_ult:
  case fmir::Opcode::cjmp_int_ule:
  case fmir::Opcode::cjmp_int_ugt:
  case fmir::Opcode::cjmp_int_uge:
  case fmir::Opcode::cjmp_int_sge:
  case fmir::Opcode::cjmp_int_slt: {
    auto a = convert_operand(cc, reg_to_op, instr.args[0]);
    auto b = convert_operand(cc, reg_to_op, instr.args[1]);
    cc.emit(Inst::kIdCmp, a, b);
    switch (instr.op) {
    case fmir::Opcode::cjmp_int_ne:
      cc.emit(Inst::kIdJne, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_int_eq:
      cc.emit(Inst::kIdJe, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_int_ult:
      cc.emit(Inst::kIdJb, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_int_ule:
      cc.emit(Inst::kIdJbe, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_int_ugt:
      cc.emit(Inst::kIdJa, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_int_uge:
      cc.emit(Inst::kIdJae, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_int_slt:
      cc.emit(Inst::kIdJl, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_int_sge:
      cc.emit(Inst::kIdJge, bb_labels[instr.bb_ref]);
      break;
    default:
      TODO("UNREAC");
    }
    return;
  }
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
  case fmir::Opcode::cjmp_flt_une: {
    auto a = convert_operand(cc, reg_to_op, instr.args[0]);
    auto b = convert_operand(cc, reg_to_op, instr.args[1]);
    bool is_ordered = instr.op == fmir::Opcode::cjmp_flt_oeq ||
                      instr.op == fmir::Opcode::cjmp_flt_ogt ||
                      instr.op == fmir::Opcode::cjmp_flt_oge ||
                      instr.op == fmir::Opcode::cjmp_flt_olt ||
                      instr.op == fmir::Opcode::cjmp_flt_ole ||
                      instr.op == fmir::Opcode::cjmp_flt_one ||
                      instr.op == fmir::Opcode::cjmp_flt_ord;
    if (instr.args[0].ty == fmir::Type::Float64) {
      cc.emit(is_ordered ? Inst::kIdComisd : Inst::kIdUcomisd, a, b);
    } else if (instr.args[0].ty == fmir::Type::Float32) {
      cc.emit(is_ordered ? Inst::kIdComiss : Inst::kIdUcomiss, a, b);
    } else {
      TODO("UNREACH");
    }
    switch (instr.op) {
    case fmir::Opcode::cjmp_flt_oeq:
    case fmir::Opcode::cjmp_flt_ueq:
      cc.emit(Inst::kIdJe, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_flt_une:
    case fmir::Opcode::cjmp_flt_one:
      cc.emit(Inst::kIdJne, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_flt_ugt:
    case fmir::Opcode::cjmp_flt_ogt:
      cc.emit(Inst::kIdJg, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_flt_uge:
    case fmir::Opcode::cjmp_flt_oge:
      cc.emit(Inst::kIdJge, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_flt_ult:
    case fmir::Opcode::cjmp_flt_olt:
      cc.emit(Inst::kIdJl, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_flt_ule:
    case fmir::Opcode::cjmp_flt_ole:
      cc.emit(Inst::kIdJle, bb_labels[instr.bb_ref]);
      break;
    case fmir::Opcode::cjmp_flt_ord:
    case fmir::Opcode::cjmp_flt_uno:
    default:
      TODO("UNREACH");
    }
    return;
  }
  case fmir::Opcode::icmp_slt: {
    auto targ = convert_operand(cc, reg_to_op, instr.args[0]);
    auto a = convert_operand(cc, reg_to_op, instr.args[1]);
    auto b = convert_operand(cc, reg_to_op, instr.args[2]);
    cc.emit(Inst::kIdCmp, a, b);
    cc.emit(Inst::kIdSetl, targ);
    return;
  }
  case fmir::Opcode::icmp_eq: {
    auto targ = convert_operand(cc, reg_to_op, instr.args[0]);
    auto a = convert_operand(cc, reg_to_op, instr.args[1]);
    auto b = convert_operand(cc, reg_to_op, instr.args[2]);
    cc.emit(Inst::kIdCmp, a, b);
    cc.emit(Inst::kIdSetl, targ);
    return;
  }
  case fmir::Opcode::ret: {
    if (instr.n_args > 0) {
      ASSERT(instr.args[0].isReg() &&
             (instr.args[0].reg.info.ty == fmir::VRegType::A ||
              instr.args[0].reg.info.ty == fmir::VRegType::mm0));
    }
    cc.emit(asmjit::x86::Inst::kIdMov, rsp, rbp);
    cc.emit(asmjit::x86::Inst::kIdPop, rbp);
    cc.emit(Inst::kIdRet);
    return;
  }
  }
}

void emit_func(const fmir::MFunc &func, TMap<fmir::VReg, Reg> reg_to_op,
               Compiler &cc) {
  TVec<Label> bb_labels;

  bb_labels.reserve(func.bbs.size());
  for (const auto &a : func.bbs) {
    (void)a;
    bb_labels.push_back(cc.newLabel());
  }

  for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
    const auto &bb = func.bbs[bb_id];
    Label label_bb = bb_labels[bb_id];
    cc.bind(label_bb);
    for (auto instr : bb.instrs) {
      emit_instr(instr, bb_labels, reg_to_op, cc);
    }
  }
}

void replaceAll(std::string &str, const std::string &from,
                const std::string &to) {
  if (from.empty()) {
    return;
  }
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // In case 'to' contains 'from', like
                              // replacing 'x' with 'yx'
  }
}

class MyErrorHandler : public ErrorHandler {
public:
  void handleError(Error err, const char *message,
                   BaseEmitter *origin) override {
    (void)err;
    (void)origin;
    printf("AsmJit error: %s\n", message);
    std::abort();
  }
};

void run(std::span<const fmir::MFunc> funcs,
         std::span<const fmir::Global> globals) {
  JitRuntime rt; // Runtime specialized for JIT code execution.
  StringLogger logger;
  CodeHolder code; // Holds code and relocation information.

  code.init(rt.environment(), // Initialize code to match the JIT environment.
            rt.cpuFeatures());
  code.setLogger(&logger);
  MyErrorHandler err_handler{};
  code.setErrorHandler(&err_handler);

  x86::Compiler cc(&code); // Create and attach x86::Compiler to code.

  // for (const auto &c : globals) {
  //   auto const_val =
  //       cc.newConst(ConstPoolScope::kGlobal, c.data.data(),
  //       c.data.size());
  // }

  cc.addDiagnosticOptions(DiagnosticOptions::kValidateAssembler);
  cc.addDiagnosticOptions(DiagnosticOptions::kValidateIntermediate);

  Reg2OpMap reg_to_op;
  TVec<Label> func_labels;
  func_labels.reserve(funcs.size());
  {
    ZoneScopedN("Assembling");
    for (auto func : funcs) {
      func_labels.push_back(cc.newNamedLabel(func.name.c_str()));
    }

    for (u32 i = 0; i < func_labels.size(); i++) {
      reg_to_op.clear();
      const fmir::MFunc &func = funcs[i];
      // auto builder =
      //     convert_func_signature(func.arg_tys, func.void_ret,
      //     func.res_ty);
      cc.bind(func_labels.at(i));
      // cc.addFunc(builder);
      cc.emit(asmjit::x86::Inst::kIdPush, rbp);
      cc.emit(asmjit::x86::Inst::kIdMov, rbp, rsp);
      // for (u32 i = 0; i < func.args.size(); i++) {
      //   auto target_reg =
      //       convert_reg(cc, reg_to_op, func.args[i], func.arg_tys[i]);
      //   cc.emit(asmjit::x86::Inst::kIdMov, target_reg,
      //           Mem(rbp, 8 * (i + 2), get_size(func.arg_tys[i])));
      // }
      emit_func(func, reg_to_op, cc);
      // cc.endFunc();
    }
  }
  // utils::Debug << "ASM:\n" << logger.data() << "\n";

  cc.finalize();

  // for (auto *section : code.sections()) {
  //   section->alignment();
  //   section->flags();
  //   section->data();
  // }

  {
    ZoneScopedN("Output");
    std::string out_string = logger.data();
    replaceAll(out_string, "ptr ", "");
    replaceAll(out_string, "\nshort ", "\n");
    replaceAll(out_string, "\nrex ", "\n");
    replaceAll(out_string, ".section .text {#0}", "\n");

    // out_string += ".section .data";
    if (globals.size() != 0) {
      out_string += "\nSECTION .data\n";
      for (const auto &global : globals) {
        out_string += global.name;
        out_string += ":\nDB ";
        for (const auto &data : global.data) {
          out_string += std::to_string(data);
          out_string += ", ";
        }
        out_string += "0\n";
      }
    }

    utils::Debug << "ASM:\n" << out_string.c_str() << "\n";
    utils::Debug << "Done!\n";

    std::ofstream myfile;
    myfile.open(utils::out_file_path);
    myfile << "global _start\n"
              "extern _memset\n"
              "_start:\n"
              "  call main\n"
              "  mov ebx, eax\n"
              "  mov eax, 1\n"
              "  int 0x80\n";
    myfile << out_string.c_str();
    myfile.close();
  }
}

} // namespace foptim::codegen
