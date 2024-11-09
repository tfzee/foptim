#pragma once
#include "utils/logging.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"
#include <string>
#include <utility>

namespace foptim::fmir {

enum class Opcode : u32 {
  mov,
  mov_zx,
  mov_sx,
  lea,

  add,
  sub,
  mul,
  idiv,

  push,
  pop,

  icmp_slt,
  icmp_eq,

  cjmp_int_slt,
  cjmp_int_sge,
  cjmp_int_ult,
  cjmp_int_ule,
  cjmp_int_ugt,
  cjmp_int_uge,
  cjmp_int_ne,
  cjmp_int_eq,
  cjmp_flt_oeq,
  cjmp_flt_ogt,
  cjmp_flt_oge,
  cjmp_flt_olt,
  cjmp_flt_ole,
  cjmp_flt_one,
  cjmp_flt_ord,
  cjmp_flt_uno,
  cjmp_flt_ueq,
  cjmp_flt_ugt,
  cjmp_flt_uge,
  cjmp_flt_ult,
  cjmp_flt_ule,
  cjmp_flt_une,

  cjmp,
  jmp,

  call,
  ret,

  arg_setup,
  invoke,
};

constexpr const char *getNameFromOpcode(Opcode code) {
  switch (code) {
  case Opcode::mov:
    return "mov";
  case Opcode::lea:
    return "lea";
  case Opcode::mov_zx:
    return "mov_zx";
  case Opcode::mov_sx:
    return "mov_sx";
  case Opcode::add:
    return "add";
  case Opcode::idiv:
    return "idiv";
  case Opcode::sub:
    return "sub";
  case Opcode::mul:
    return "mul";
  case Opcode::icmp_slt:
    return "icmp_slt";
  case Opcode::icmp_eq:
    return "icmp_eq";
  case Opcode::jmp:
    return "jmp";
  case Opcode::call:
    return "call";
  case Opcode::push:
    return "push";
  case Opcode::pop:
    return "pop";
  case Opcode::cjmp:
    return "cjmp";
  case Opcode::ret:
    return "ret";
  case Opcode::arg_setup:
    return "#arg_setup";
  case Opcode::invoke:
    return "#invoke";
  case Opcode::cjmp_int_slt:
    return "cjmp_int_slt";
  case Opcode::cjmp_int_sge:
    return "cjmp_int_sge";
  case Opcode::cjmp_int_ne:
    return "cjmp_int_ne";
  case Opcode::cjmp_int_eq:
    return "cjmp_int_eq";
  case Opcode::cjmp_int_ult:
    return "cjmp_int_ult";
  case Opcode::cjmp_int_ule:
    return "cjmp_int_ule";
  case Opcode::cjmp_int_uge:
    return "cjmp_int_uge";
  case Opcode::cjmp_int_ugt:
    return "cjmp_int_ugt";
  case Opcode::cjmp_flt_oeq:
    return "cjmp_flt_oeq";
  case Opcode::cjmp_flt_ogt:
    return "cjmp_flt_ogt";
  case Opcode::cjmp_flt_oge:
    return "cjmp_flt_oge";
  case Opcode::cjmp_flt_olt:
    return "cjmp_flt_olt";
  case Opcode::cjmp_flt_ole:
    return "cjmp_flt_ole";
  case Opcode::cjmp_flt_one:
    return "cjmp_flt_one";
  case Opcode::cjmp_flt_ord:
    return "cjmp_flt_ord";
  case Opcode::cjmp_flt_uno:
    return "cjmp_flt_uno";
  case Opcode::cjmp_flt_ueq:
    return "cjmp_flt_ueq";
  case Opcode::cjmp_flt_ugt:
    return "cjmp_flt_ugt";
  case Opcode::cjmp_flt_uge:
    return "cjmp_flt_uge";
  case Opcode::cjmp_flt_ult:
    return "cjmp_flt_ult";
  case Opcode::cjmp_flt_ule:
    return "cjmp_flt_ule";
  case Opcode::cjmp_flt_une:
    return "cjmp_flt_une";
  }
}

enum class Type : u16 {
  INVALID = 0,
  Int8 = 1,
  Int16 = 2,
  Int32 = 3,
  Int64 = 4,

  Float32 = 5,
  Float64 = 6,

};

/*Returns the size in bytes of the given type*/
static constexpr u32 get_size(fmir::Type type) {
  switch (type) {
  case fmir::Type::Float32:
    return 4;
  case fmir::Type::Float64:
    return 8;
  case fmir::Type::Int8:
    return 1;
  case fmir::Type::Int16:
    return 2;
  case fmir::Type::Int32:
    return 4;
  case fmir::Type::Int64:
    return 8;
  case fmir::Type::INVALID:
    break;
  }
  ASSERT(false);
  std::abort();
}

enum class VRegType : u8 {
  Virtual = 0,
  A = 1,
  B,
  C,
  D,
  S,
  SP,
  BP,
  R8,
  R9,
  R10,
  R11,
  R12,
  R13,
  R14,
  R15 = 15,
};

struct VRegInfo {
  VRegType ty;
  u8 reg_size;

  constexpr VRegInfo() : ty(VRegType::Virtual), reg_size(0) {}
  explicit constexpr VRegInfo(u8 size)
      : ty(VRegType::Virtual), reg_size(size) {}

  explicit constexpr VRegInfo(VRegType ty, u8 size) : ty(ty), reg_size(size) {}

  static constexpr VRegInfo RSP() {
    VRegInfo res{};
    res.ty = VRegType::SP;
    res.reg_size = 8;
    return res;
  }

  static constexpr VRegInfo ESP() {
    VRegInfo res{};
    res.ty = VRegType::SP;
    res.reg_size = 4;
    return res;
  }

  static constexpr VRegInfo RAX() {
    VRegInfo res{};
    res.ty = VRegType::A;
    res.reg_size = 8;
    return res;
  }

  static constexpr VRegInfo EAX() {
    VRegInfo res{};
    res.ty = VRegType::A;
    res.reg_size = 4;
    return res;
  }

  static constexpr VRegInfo EDX() {
    VRegInfo res{};
    res.ty = VRegType::D;
    res.reg_size = 4;
    return res;
  }

  [[nodiscard]] constexpr bool is_pinned() const {
    return ty != VRegType::Virtual;
  }

  constexpr bool operator==(const VRegInfo &other) const {
    return ty == other.ty && reg_size == other.reg_size;
  }
};

class VReg {
public:
  size_t id;
  VRegInfo info;

  constexpr VReg() : id(0) {}
  constexpr VReg(u64 id) : id(id) {}
  constexpr VReg(u64 id, VRegInfo info) : id(id), info(info) {}
  constexpr VReg(u64 id, u8 size) : id(id), info(size) {}

  static constexpr VReg EAX() {
    VRegInfo res_info{};
    res_info.ty = VRegType::A;
    res_info.reg_size = 4;
    VReg res{0, res_info};
    return res;
  }

  static constexpr VReg RSP() {
    VRegInfo res_info{};
    res_info.ty = VRegType::SP;
    res_info.reg_size = 8;
    VReg res{0, res_info};
    return res;
  }

  constexpr bool operator==(const VReg &other) const {
    const bool info_matches = info == other.info;
    if (info.ty == VRegType::Virtual) {
      return id == other.id && info_matches;
    }
    return info_matches;
  }
};

class MArgument {
public:
  enum class ArgumentType : u8 {
    Imm,
    VReg,
    MemVReg,
    MemVRegVReg,
    MemImm,
    MemImmVReg,
    MemImmVRegVReg,
    MemVRegVRegScale,
    MemImmVRegScale,
    MemImmVRegVRegScale,

    Label,
    MemLabel,
    MemImmLabel,
  };

  ArgumentType type;
  Type ty;

  u64 imm;
  u64 scale;
  VReg reg;
  VReg indx;
  std::string label;

  constexpr MArgument() : type(ArgumentType::Imm), imm(0) {}
  // TODO FIX TYPE CONVERSION HERE
  constexpr MArgument(VReg reg, Type ty)
      : type(ArgumentType::VReg), ty(ty), reg(reg) {
    reg.info.reg_size = get_size(ty);
  }
  MArgument(u64 imm) : type(ArgumentType::Imm), ty(Type::Int64), imm(imm) {}
  MArgument(std::string lab)
      : type(ArgumentType::Label), ty(Type::INVALID), label(std::move(lab)) {}
  MArgument(std::string lab, Type ty)
      : type(ArgumentType::Label), ty(ty), label(std::move(lab)) {}

  [[nodiscard]] static constexpr MArgument Mem(std::string lab, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemLabel;
    arg.ty = ty;
    arg.label = lab;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(std::string lab, u32 imm,
                                               Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImmLabel;
    arg.ty = ty;
    arg.label = lab;
    arg.imm = imm;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(VReg reg, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVReg;
    arg.ty = ty;
    arg.reg = reg;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(VReg reg, u32 off, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImmVReg;
    arg.ty = ty;
    arg.imm = off;
    arg.reg = reg;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(VReg reg, VReg indx, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVRegVReg;
    arg.ty = ty;
    arg.reg = reg;
    arg.indx = indx;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(VReg reg, VReg indx, u32 scale,
                                               Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVRegVRegScale;
    arg.ty = ty;
    arg.reg = reg;
    arg.indx = indx;
    arg.scale = scale;
    return arg;
  }

  [[nodiscard]] constexpr bool isReg() const {
    return type == ArgumentType::VReg;
  }
  [[nodiscard]] constexpr bool isImm() const {
    return type == ArgumentType::Imm;
  }
  [[nodiscard]] constexpr bool isLabel() const {
    return type == ArgumentType::Label;
  }

  constexpr bool operator==(const MArgument &other) const {
    if (type != other.type || ty != other.ty) {
      return false;
    }
    switch (type) {
    case ArgumentType::MemLabel:
    case ArgumentType::Label:
      return label == other.label;
    case ArgumentType::MemImm:
    case ArgumentType::Imm:
      return imm == other.imm;
    case ArgumentType::VReg:
    case ArgumentType::MemVReg:
      return reg == other.reg;
    case ArgumentType::MemVRegVReg:
    case ArgumentType::MemImmVReg:
    case ArgumentType::MemImmVRegVReg:
    case ArgumentType::MemVRegVRegScale:
    case ArgumentType::MemImmVRegScale:
    case ArgumentType::MemImmVRegVRegScale:
    case ArgumentType::MemImmLabel:
      TODO("impl");
      break;
      break;
    }
  }

  [[nodiscard]] constexpr bool uses_same_vreg(const MArgument &other) const {
    switch (type) {
      // no regs
    case ArgumentType::Imm:
    case ArgumentType::MemImm:
    case ArgumentType::Label:
    case ArgumentType::MemLabel:
    case ArgumentType::MemImmLabel:
      return false;

      // 1 reg
    case ArgumentType::VReg:
    case ArgumentType::MemVReg:
    case ArgumentType::MemImmVReg:
    case ArgumentType::MemImmVRegScale:
      switch (other.type) {
      case ArgumentType::MemImmLabel:
      case ArgumentType::Imm:
      case ArgumentType::MemImm:
      case ArgumentType::Label:
      case ArgumentType::MemLabel:
        return false;
      // 1x1 reg
      case ArgumentType::VReg:
      case ArgumentType::MemVReg:
      case ArgumentType::MemImmVReg:
      case ArgumentType::MemImmVRegScale:
        return other.reg == reg;
      // 1x2 reg
      case ArgumentType::MemVRegVReg:
      case ArgumentType::MemImmVRegVReg:
      case ArgumentType::MemVRegVRegScale:
      case ArgumentType::MemImmVRegVRegScale:
        return other.reg == reg || other.indx == reg;
        break;
      }
    // 2 reg
    case ArgumentType::MemVRegVReg:
    case ArgumentType::MemImmVRegVReg:
    case ArgumentType::MemVRegVRegScale:
    case ArgumentType::MemImmVRegVRegScale:
      switch (other.type) {
      case ArgumentType::MemImmLabel:
      case ArgumentType::Imm:
      case ArgumentType::MemImm:
      case ArgumentType::Label:
      case ArgumentType::MemLabel:
        return false;
      // 2x1 reg
      case ArgumentType::VReg:
      case ArgumentType::MemVReg:
      case ArgumentType::MemImmVReg:
      case ArgumentType::MemImmVRegScale:
        return other.reg == reg || other.reg == indx;
      // 2x2 reg
      case ArgumentType::MemVRegVReg:
      case ArgumentType::MemImmVRegVReg:
      case ArgumentType::MemVRegVRegScale:
      case ArgumentType::MemImmVRegVRegScale:
        return other.reg == reg || other.reg == indx || other.indx == reg ||
               other.indx == indx;
      }
    }
    TODO("unreach");
  }

  [[nodiscard]] constexpr bool uses_vreg() const {
    switch (type) {
    case ArgumentType::MemImmLabel:
    case ArgumentType::Imm:
    case ArgumentType::Label:
    case ArgumentType::MemImm:
    case ArgumentType::MemLabel:
      return false;
    case ArgumentType::VReg:
    case ArgumentType::MemVReg:
    case ArgumentType::MemVRegVReg:
    case ArgumentType::MemImmVReg:
    case ArgumentType::MemImmVRegVReg:
    case ArgumentType::MemVRegVRegScale:
    case ArgumentType::MemImmVRegScale:
    case ArgumentType::MemImmVRegVRegScale:
      return true;
    }
  }
};

class MInstr {
public:
  bool has_bb_ref;
  u8 n_args;
  Opcode op;
  u32 bb_ref;
  MArgument args[4];

  MInstr(Opcode op) : has_bb_ref(false), n_args(0), op(op) {}
  MInstr(Opcode op, MArgument a1) : has_bb_ref(false), n_args(1), op(op) {
    args[0] = a1;
  }
  MInstr(Opcode op, MArgument a1, MArgument a2)
      : has_bb_ref(false), n_args(2), op(op) {
    args[0] = a1;
    args[1] = a2;
  }
  MInstr(Opcode op, MArgument a1, MArgument a2, MArgument a3)
      : has_bb_ref(false), n_args(3), op(op) {
    args[0] = a1;
    args[1] = a2;
    args[2] = a3;
  }
  MInstr(Opcode op, MArgument a1, MArgument a2, MArgument a3, MArgument a4)
      : has_bb_ref(false), n_args(4), op(op) {
    args[0] = a1;
    args[1] = a2;
    args[2] = a3;
    args[3] = a4;
  }

  static MInstr jmp(u32 new_bb_ref) {
    auto res = MInstr{Opcode::jmp};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp(MArgument cond, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp, cond};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp_slt(MArgument v1, MArgument v2, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp_int_slt, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp_sge(MArgument v1, MArgument v2, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp_int_sge, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp_ult(MArgument v1, MArgument v2, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp_int_ult, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp_ule(MArgument v1, MArgument v2, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp_int_ule, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp_ugt(MArgument v1, MArgument v2, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp_int_ugt, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp_uge(MArgument v1, MArgument v2, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp_int_uge, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp_eq(MArgument v1, MArgument v2, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp_int_eq, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp_ne(MArgument v1, MArgument v2, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp_int_ne, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }
};

} // namespace foptim::fmir

template <> struct std::hash<foptim::fmir::VReg> {
  std::size_t operator()(const foptim::fmir::VReg &k) const {
    using foptim::u64;
    using foptim::u8;
    if (k.info.is_pinned()) {
      return hash<u8>()((u8)k.info.ty);
    }
    return hash<u64>()(k.id);
  }
};
