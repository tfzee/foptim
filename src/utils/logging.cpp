#include "logging.hpp"
#include "ir/attribute.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "optim/analysis/live_variables.hpp"
#include <iostream>

namespace foptim::utils {

Printer Printer::operator<<(const i64 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const i32 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const i16 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const i8 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const u64 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const u32 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const u16 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const u8 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const bool val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const f32 val) const {
  std::cout << val;
  return *this;
}
Printer Printer::operator<<(const f64 val) const {
  std::cout << val;
  return *this;
}

Printer Printer::operator<<(PaddingT pad) const {
  (void)pad;
  for (u32 i = indent; i > 0; i--) {
    std::cout << " ";
  }
  return *this;
}

// Printer Printer::operator<<(const foptim::utils::BitSet &bitset) const {
//   for (auto bit : bitset._data) {
//     std::cout << bit;
//   }
//   return *this;
// }

Printer Printer::operator<<(const foptim::fir::Use &use) const {
  *this << (void *)use.user.get_raw_ptr();
  switch (use.type) {
  case fir::UseType::NormalArg:
    *this << "[N " << use.argId << "]";
    break;
  case fir::UseType::BB:
    *this << "[B " << use.argId << "]";
    break;
  case fir::UseType::BBArg:
    *this << "[BA " << use.argId << "; " << use.bbArgId << "]";
    break;
  }
  return *this;
}
Printer Printer::operator<<(const foptim::optim::LiveRange &live) const {
  return *this << live.bb << "(" << live.start << ", " << live.end << ")";
}

Printer Printer::operator<<(const foptim::fmir::MArgument &value) const {
  switch (value.type) {
  case fmir::MArgument::ArgumentType::MemLabel:
    return *this << "[" << value.label.c_str() << "]: " << value.ty;
  case fmir::MArgument::ArgumentType::MemImmLabel:
    return *this << "[" << value.label.c_str() << " + " << value.imm << "]"
                 << value.ty;
  case fmir::MArgument::ArgumentType::Label:
    return *this << value.label.c_str();
  case fmir::MArgument::ArgumentType::Imm: {
    if (value.ty == fmir::Type::Float32) {
      return *this << value.immf << "f";
    }
    if (value.ty == fmir::Type::Float64) {
      return *this << value.immf;
    }
    return *this << value.imm << ":" << value.ty;
  }
  case fmir::MArgument::ArgumentType::VReg:
    return *this << value.reg << ":" << value.ty;
  case fmir::MArgument::ArgumentType::MemVReg:
    return *this << "[" << value.reg << "]:" << value.ty;
  case fmir::MArgument::ArgumentType::MemVRegVReg:
    return *this << "[" << value.reg << " + " << value.indx << "]:" << value.ty;
  case fmir::MArgument::ArgumentType::MemImm:
    return *this << "[" << value.imm << "]:" << value.ty;
  case fmir::MArgument::ArgumentType::MemImmVReg:
    return *this << "[" << value.reg << " + " << value.imm << "]:" << value.ty;
  case fmir::MArgument::ArgumentType::MemImmVRegVReg:
    return *this << "[" << value.reg << " + " << value.indx << " + "
                 << value.imm << "]:" << value.ty;
  case fmir::MArgument::ArgumentType::MemImmVRegScale:
    return *this << "[" << value.indx << "*" << value.scale << " + "
                 << value.imm << "]:" << value.ty;
  case fmir::MArgument::ArgumentType::MemVRegVRegScale:
    return *this << "[" << value.reg << " + " << value.indx << "*"
                 << value.scale << "]:" << value.ty;
  case fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
    return *this << "[" << value.reg << " + " << value.indx << "*"
                 << value.scale << " + " << value.imm << "]:" << value.ty;
  }
}
Printer Printer::operator<<(const foptim::fmir::MBB &bb) const {
  for (const auto &instr : bb.instrs) {
    this->pad(4) << padding() << instr << "\n";
  }
  return *this;
}

Printer Printer::operator<<(const foptim::fmir::MFunc &func) const {
  *this << "func " << func.name.c_str() << "<";
  const auto &attribs = func.get_attribs();
  for (auto [key, value] : attribs) {
    *this << key.c_str() << value << ", ";
  }
  *this << ">";

  *this << "(";
  auto n_args = func.args.size();
  for (u32 i = 0; i < n_args; i++) {
    *this << func.args[i] << ": " << func.arg_tys[i] << ", ";
  }
  *this << ")\n";

  for (size_t bb_indx = 0; bb_indx < func.bbs.size(); bb_indx++) {
    this->pad(2) << padding() << bb_indx << ":" << "\n" << func.bbs[bb_indx];
  }
  return *this;
}
Printer Printer::operator<<(const foptim::fmir::MInstr &value) const {
  switch (value.op) {
  case fmir::Opcode::mov:
    return *this << value.args[0] << " = " << value.args[1];
  // case fmir::Opcode::cmov:
  //   return *this << " if " <<value.args[1] << " " << value.args[0] << " = "
  //                 << value.args[2];
  case fmir::Opcode::add:
    return *this << value.args[0] << " = " << value.args[1] << " + "
                 << value.args[2];
  case fmir::Opcode::sub:
    return *this << value.args[0] << " = " << value.args[1] << " - "
                 << value.args[2];
  case fmir::Opcode::mul:
    return *this << value.args[0] << " = " << value.args[1] << " * "
                 << value.args[2];
  default:
    *this << getNameFromOpcode(value.op) << "(";
    for (size_t arg_indx = 0; arg_indx < value.n_args; arg_indx++) {
      *this << value.args[arg_indx] << ", ";
    }
    *this << ")";
    if (value.has_bb_ref) {
      *this << " -> " << value.bb_ref;
    }
    return *this;
  }
}

Printer Printer::operator<<(const foptim::fmir::Type &ty) const {
  switch (ty) {
  case foptim::fmir::Type::INVALID:
    return *this << "INVALID";
  case foptim::fmir::Type::Float32:
    return *this << "F32";
  case foptim::fmir::Type::Float64:
    return *this << "F64";
  case foptim::fmir::Type::Int8:
    return *this << "I8";
  case foptim::fmir::Type::Int16:
    return *this << "I16";
  case foptim::fmir::Type::Int32:
    return *this << "I32";
  case foptim::fmir::Type::Int64:
    return *this << "I64";
  }
}

Printer Printer::operator<<(const foptim::fmir::VReg &value) const {
  using foptim::fmir::VRegType;

  if (value.info.reg_size == 0) {
    switch (value.info.ty) {
    case VRegType::A:
      return *this << "$a";
    case VRegType::SP:
      return *this << "$sp";
    case VRegType::B:
      return *this << "$b";
    case VRegType::C:
      return *this << "$c";
    case VRegType::D:
      return *this << "$d";
    case VRegType::DI:
      return *this << "$di";
    case VRegType::SI:
      return *this << "$si";
    case VRegType::BP:
      return *this << "$bp";
    case VRegType::R8:
      return *this << "$8";
    case VRegType::R9:
      return *this << "$9";
    case VRegType::R10:
      return *this << "$10";
    case VRegType::R11:
      return *this << "$11";
    case VRegType::R12:
      return *this << "$12";
    case VRegType::R13:
      return *this << "$13";
    case VRegType::R14:
      return *this << "$14";
    case VRegType::R15:
      return *this << "$15";
    default:
    }
  } else if (value.info.reg_size == 1) {
    switch (value.info.ty) {
    case VRegType::A:
      return *this << "$al";
    case VRegType::SP:
      return *this << "$spl";
    case VRegType::B:
      return *this << "$bl";
    case VRegType::C:
      return *this << "$cl";
    case VRegType::D:
      return *this << "$dl";
    case VRegType::DI:
      return *this << "$ldi";
    case VRegType::SI:
      return *this << "$lsi";
    case VRegType::BP:
      return *this << "$lbp";
    case VRegType::R8:
      return *this << "$r8l";
    case VRegType::R9:
      return *this << "$r9l";
    case VRegType::R10:
      return *this << "$r10l";
    case VRegType::R11:
      return *this << "$r11l";
    case VRegType::R12:
      return *this << "$r12l";
    case VRegType::R13:
      return *this << "$r13l";
    case VRegType::R14:
      return *this << "$r14l";
    case VRegType::R15:
      return *this << "$r15l";
    default:
    }
  } else if (value.info.reg_size == 2) {
    switch (value.info.ty) {
    case VRegType::A:
      return *this << "$ax";
    case VRegType::SP:
      return *this << "$sp";
    case VRegType::B:
    case VRegType::C:
    case VRegType::D:
    case VRegType::DI:
    case VRegType::SI:
    case VRegType::BP:
    case VRegType::R8:
    case VRegType::R9:
    case VRegType::R10:
    case VRegType::R11:
    case VRegType::R12:
    case VRegType::R13:
    case VRegType::R14:
    case VRegType::R15:
      return *this << "$$2$$";
    default:
    }
  } else if (value.info.reg_size == 4) {
    switch (value.info.ty) {
    case VRegType::A:
      return *this << "$eax";
    case VRegType::SP:
      return *this << "$esp";
    case VRegType::B:
      return *this << "$ebx";
    case VRegType::C:
      return *this << "$ecx";
    case VRegType::D:
      return *this << "$edx";
    case VRegType::DI:
      return *this << "$edi";
    case VRegType::SI:
      return *this << "$esi";
    case VRegType::BP:
      return *this << "$ebp";
    case VRegType::R8:
      return *this << "$r8d";
    case VRegType::R9:
      return *this << "$r9d";
    case VRegType::R10:
      return *this << "$r10d";
    case VRegType::R11:
      return *this << "$r11d";
    case VRegType::R12:
      return *this << "$r12d";
    case VRegType::R13:
      return *this << "$r13d";
    case VRegType::R14:
      return *this << "$r14d";
    case VRegType::R15:
      return *this << "$r15d";
    case VRegType::mm0:
    case VRegType::mm1:
    case VRegType::mm2:
    case VRegType::mm3:
    case VRegType::mm4:
    case VRegType::mm5:
    case VRegType::mm6:
    case VRegType::mm7:
    case VRegType::mm8:
    case VRegType::mm9:
    case VRegType::mm10:
    case VRegType::mm11:
    case VRegType::mm12:
    case VRegType::mm13:
    case VRegType::mm14:
    case VRegType::mm15:
      return *this << "$mm" << ((u8)value.info.ty - (u8)VRegType::mm0);
    default:
    }
  } else if (value.info.reg_size == 8) {
    switch (value.info.ty) {
    case VRegType::A:
      return *this << "$rax";
    case VRegType::SP:
      return *this << "$rsp";
    case VRegType::B:
      return *this << "$rbx";
    case VRegType::C:
      return *this << "$rcx";
    case VRegType::D:
      return *this << "$rdx";
    case VRegType::DI:
      return *this << "$rdi";
    case VRegType::SI:
      return *this << "$rsi";
    case VRegType::BP:
      return *this << "$rbp";
    case VRegType::R8:
      return *this << "$r8";
    case VRegType::R9:
      return *this << "$r9";
    case VRegType::R10:
      return *this << "$r10";
    case VRegType::R11:
      return *this << "$r11";
    case VRegType::R12:
      return *this << "$r12";
    case VRegType::R13:
      return *this << "$r13";
    case VRegType::R14:
      return *this << "$r14";
    case VRegType::R15:
      return *this << "$r15";
    case VRegType::mm0:
    case VRegType::mm1:
    case VRegType::mm2:
    case VRegType::mm3:
    case VRegType::mm4:
    case VRegType::mm5:
    case VRegType::mm6:
    case VRegType::mm7:
    case VRegType::mm8:
    case VRegType::mm9:
    case VRegType::mm10:
    case VRegType::mm11:
    case VRegType::mm12:
    case VRegType::mm13:
    case VRegType::mm14:
    case VRegType::mm15:
      return *this << "$mm" << ((u8)value.info.ty - (u8)VRegType::mm0);
    default:
    }
  }
  return *this << "$" << value.id;
}

Printer Printer::operator<<(const char *str) const {
  std::cout << str;
  return *this;
}

Printer Printer::operator<<(const void *v) const {
  std::cout << v;
  return *this;
}

// Printer Printer::operator<<(const std::string &data) const {
//   std::cout << data;
//   return *this;
// }

Printer Printer::operator<<(foptim::fir::TypeR ty) const {
  std::visit(
      [this](auto &&v) {
        if constexpr (typeid(v) == typeid(fir::IntegerType)) {
          *this << "i" << v.bitwidth;
        } else if constexpr (typeid(v) == typeid(fir::FloatType)) {
          *this << "f" << v.bitwidth;
        } else if constexpr (typeid(v) == typeid(fir::VoidType)) {
          *this << "()";
        } else if constexpr (typeid(v) == typeid(fir::OpaquePointerType)) {
          *this << "ptr";
        } else {
          *this << typeid(v).name();
        }
      },
      ty->get_raw());
  return *this;
}

Printer Printer::operator<<(const foptim::fir::ConstantValue &v) const {
  std::visit(
      [this](auto &&v) {
        (void)v;
        if constexpr (typeid(v) == typeid(fir::IntValue) ||
                      typeid(v) == typeid(fir::FloatValue)) {
          *this << v.data;
        } else if constexpr (typeid(v) == typeid(fir::GlobalPointer)) {
          *this << "G(" << (void *)(v.glob.get_raw_ptr()) << ")";
        } else {
          *this << "constant idk(" << typeid(v).name() << ")";
        }
      },
      v.value);
  return *this;
}

Printer Printer::operator<<(const foptim::fir::ConstantValueR v) const {
  return *this << *v.get_raw_ptr();
}

Printer Printer::operator<<(foptim::fir::ValueR v) const {
  std::visit(
      [this](auto &&v) {
        if constexpr (typeid(v) == typeid(fir::ConstantValueR)) {
          *this << v;
        } else if constexpr (typeid(v) == typeid(fir::BBArgumentR)) {
          *this << (void *)(v.bb.get_raw_ptr()) << "[" << v.arg << "]";
        } else if constexpr (typeid(v) == typeid(fir::BasicBlock)) {
          *this << (void *)(v.get_raw_ptr());
        } else if constexpr (typeid(v) == typeid(fir::InvalidValue)) {
          *this << "INVALID";
        } else {
          *this << (void *)v.get_raw_ptr();
        }
      },
      v.get_raw());
  return *this;
}

Printer Printer::operator<<(const foptim::fir::Attribute &attrib) const {
  std::visit(
      [this](auto &&v) {
        if constexpr (typeid(v) == typeid(fir::VoidAttrib)) {
          return;
        } else if constexpr (typeid(v) == typeid(std::string)) {
          *this << ": " << v.c_str();
        } else {
          *this << ": " << v;
        }
      },
      attrib.get_raw());
  return *this;
}
Printer
Printer::operator<<(const foptim::fir::BBRefWithArgs &bb_with_args) const {
  *this << (void *)bb_with_args.bb.get_raw_ptr() << "(";
  if (!bb_with_args.args.empty()) {
    *this << bb_with_args.args[0];
    for (size_t i = 1; i < bb_with_args.args.size(); i++) {
      *this << ", " << bb_with_args.args[i];
    }
  }
  *this << ")";
  return *this;
}

Printer Printer::operator<<(const foptim::fir::Instr instr) const {
  return *this << instr.get_raw_ptr();
}

Printer Printer::operator<<(const foptim::fir::InstrData *instr) const {
  *this << padding() << (void *)instr << " : " << instr->get_type() << " = "
        << instr->get_name();

  const auto &bb_args = instr->get_bb_args();
  if (bb_args.size() > 0) {
    *this << "<";
    *this << bb_args[0];
    for (size_t i = 1; i < bb_args.size(); i++) {
      *this << ", " << bb_args[i];
    }
    *this << ">";
  }

  *this << "(";
  const auto &args = instr->get_args();
  if (args.size() > 0) {
    *this << args[0];
    for (size_t i = 1; i < args.size(); i++) {
      *this << ", " << args[i];
    }
  }
  *this << "){";

  const auto &attribs = instr->get_attribs();
  for (auto [key, value] : attribs) {
    *this << key.c_str() << value << ", ";
  }
  *this << "}";
  //*this << "          //Uses: " << instr->get_n_uses();
  *this << "\n";
  return *this;
}

Printer Printer::operator<<(foptim::fir::BasicBlock bb) const {
  *this << padding() << bb.get_raw_ptr() << "(";

  auto &args = bb->args;
  if (args.size() > 0) {
    *this << args[0].type;
    //*this << " uses:" << args[0].get_n_uses();
    for (size_t i = 1; i < args.size(); i++) {
      *this << ", " << args[i].type;
      //*this << " uses:" << args[i].get_n_uses();
    }
  }

  *this << "):";
  //*this << "       //Uses:" << bb->get_n_uses()
  *this << "\n";
  for (fir::Instr instr : bb->get_instrs()) {
    pad(2) << instr;
  }
  return *this;
}

Printer Printer::operator<<(foptim::fir::FunctionR func) const {
  return *this << *func.operator->();
}

Printer Printer::operator<<(const foptim::fir::Function &func) const {
  padding();
  if (g_log_level <= level) {
    std::cout << "\nfunc " << func.getName();

    std::cout << "<" << "CC: ";
    switch (func.cc) {
    case fir::Function::CallingConv::C:
      std::cout << "C";
      break;
    case fir::Function::CallingConv::Dynamic:
      std::cout << "dyn";
      break;
    }
    std::cout << ", LINK: ";
    switch (func.linkage) {
    case fir::Function::Linkage::Internal:
      std::cout << "internal";
      break;
    case fir::Function::Linkage::External:
      std::cout << "external";
      break;
    }
    std::cout << ", ";
    const auto &attribs = func.get_attribs();
    for (auto [key, value] : attribs) {
      *this << key.c_str() << value << ", ";
    }

    std::cout << ">\n{\n";
    for (fir::BasicBlock bb : func.get_bbs()) {
      pad(2) << bb;
    }
    std::cout << "}";
  }
  return *this;
}

Printer Printer::operator<<(const foptim::fir::IRLocation &loc) const {
  switch (loc.type) {
  case fir::IRLocation::LocationType::Function:
    *this << (void *)loc.func.func;
    break;
  case fir::IRLocation::LocationType::Instruction:
    *this << (void *)loc.func.func << " @BB " << loc.bb << " @I " << loc.instr;
    break;
  case fir::IRLocation::LocationType::BasicBlock:
    *this << (void *)loc.func.func << " @BB " << loc.bb;
    break;
  case fir::IRLocation::LocationType::INVALID:
    *this << " @INVALID ";
    break;
  }
  return *this;
}
} // namespace foptim::utils
