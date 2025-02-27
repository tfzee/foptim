#include "instr.hpp"
#include "mir/func.hpp"
namespace foptim::fmir {

#define ReturnString(name)                                                     \
  case Opcode::name:                                                           \
    return #name;

const char *getNameFromOpcode(Opcode code) {
  switch (code) {
    ReturnString(icmp_ult);
    ReturnString(icmp_ne);
    ReturnString(icmp_sgt);
    ReturnString(icmp_ugt);
    ReturnString(icmp_uge);
    ReturnString(icmp_ule);
    ReturnString(icmp_sge);
    ReturnString(icmp_sle);
    ReturnString(fcmp_isNaN);
    ReturnString(fcmp_oeq);
    ReturnString(fcmp_ogt);
    ReturnString(fcmp_oge);
    ReturnString(fcmp_olt);
    ReturnString(fcmp_ole);
    ReturnString(fcmp_one);
    ReturnString(fcmp_ord);
    ReturnString(fcmp_uno);
    ReturnString(fcmp_ueq);
    ReturnString(fcmp_ugt);
    ReturnString(fcmp_uge);
    ReturnString(fcmp_ult);
    ReturnString(fcmp_ule);
    ReturnString(fcmp_une);
    ReturnString(mov);
    ReturnString(cmov);
    ReturnString(lea);
    ReturnString(itrunc);
    ReturnString(mov_zx);
    ReturnString(mov_sx);
    ReturnString(idiv);
    ReturnString(add2);
    ReturnString(sub2);
    ReturnString(mul2);
    ReturnString(shl2);
    ReturnString(shr2);
    ReturnString(sar2);
    ReturnString(land2);
    ReturnString(lor2);
    ReturnString(lxor2);
    ReturnString(fadd);
    ReturnString(fsub);
    ReturnString(fmul);
    ReturnString(ffmadd132);
    ReturnString(ffmadd231);
    ReturnString(ffmadd213);
    ReturnString(fxor);
    ReturnString(fAnd);
    ReturnString(fOr);
    ReturnString(icmp_slt);
    ReturnString(icmp_eq);
    ReturnString(jmp);
    ReturnString(call);
    ReturnString(push);
    ReturnString(pop);
    ReturnString(cjmp);
    ReturnString(ret);
    ReturnString(arg_setup);
    ReturnString(invoke);
    ReturnString(cjmp_int_slt);
    ReturnString(cjmp_int_sge);
    ReturnString(cjmp_int_sle);
    ReturnString(cjmp_int_sgt);
    ReturnString(cjmp_int_ne);
    ReturnString(cjmp_int_eq);
    ReturnString(cjmp_int_ult);
    ReturnString(cjmp_int_ule);
    ReturnString(cjmp_int_uge);
    ReturnString(cjmp_int_ugt);
    ReturnString(cjmp_flt_oeq);
    ReturnString(cjmp_flt_ogt);
    ReturnString(cjmp_flt_oge);
    ReturnString(cjmp_flt_olt);
    ReturnString(cjmp_flt_ole);
    ReturnString(cjmp_flt_one);
    ReturnString(cjmp_flt_ord);
    ReturnString(cjmp_flt_uno);
    ReturnString(cjmp_flt_ueq);
    ReturnString(cjmp_flt_ugt);
    ReturnString(cjmp_flt_uge);
    ReturnString(cjmp_flt_ult);
    ReturnString(cjmp_flt_ule);
    ReturnString(cjmp_flt_une);
    ReturnString(fdiv);
    ReturnString(SI2FL);
    ReturnString(UI2FL);
    ReturnString(FL2SI);
    ReturnString(FL2UI);
  }
}
#undef ReturnString

void written_args(const MInstr &instr, TVec<MArgument> &out) {
  switch (instr.op) {
  case Opcode::cmov:
  case Opcode::mov_zx:
  case Opcode::mov_sx:
  case Opcode::mov:
  case Opcode::itrunc:
  case Opcode::lea:
  case Opcode::shl2:
  case Opcode::shr2:
  case Opcode::sar2:
  case Opcode::land2:
  case Opcode::lor2:
  case Opcode::lxor2:
  case Opcode::add2:
  case Opcode::sub2:
  case Opcode::mul2:
  case Opcode::fadd:
  case Opcode::fsub:
  case Opcode::fmul:
  case Opcode::fdiv:
  case Opcode::ffmadd132:
  case Opcode::ffmadd213:
  case Opcode::ffmadd231:
  case Opcode::fxor:
  case Opcode::fOr:
  case Opcode::fAnd:
  case Opcode::SI2FL:
  case Opcode::UI2FL:
  case Opcode::FL2SI:
  case Opcode::FL2UI:
  case Opcode::pop:
  case Opcode::icmp_slt:
  case Opcode::icmp_eq:
  case Opcode::icmp_ult:
  case Opcode::icmp_ne:
  case Opcode::icmp_sgt:
  case Opcode::icmp_ugt:
  case Opcode::icmp_uge:
  case Opcode::icmp_ule:
  case Opcode::icmp_sge:
  case Opcode::icmp_sle:
  case Opcode::fcmp_isNaN:
  case Opcode::fcmp_oeq:
  case Opcode::fcmp_ogt:
  case Opcode::fcmp_oge:
  case Opcode::fcmp_olt:
  case Opcode::fcmp_ole:
  case Opcode::fcmp_one:
  case Opcode::fcmp_ord:
  case Opcode::fcmp_uno:
  case Opcode::fcmp_ueq:
  case Opcode::fcmp_ugt:
  case Opcode::fcmp_uge:
  case Opcode::fcmp_ult:
  case Opcode::fcmp_ule:
  case Opcode::fcmp_une:
    out.push_back(instr.args[0]);
    return;
  case Opcode::idiv:
    out.push_back(instr.args[0]);
    out.push_back(instr.args[1]);
    return;
  case Opcode::call:
  case Opcode::push:
  case Opcode::cjmp_int_slt:
  case Opcode::cjmp_int_sge:
  case Opcode::cjmp_int_sle:
  case Opcode::cjmp_int_sgt:
  case Opcode::cjmp_int_ult:
  case Opcode::cjmp_int_ule:
  case Opcode::cjmp_int_ugt:
  case Opcode::cjmp_int_uge:
  case Opcode::cjmp_int_ne:
  case Opcode::cjmp_int_eq:
  case Opcode::cjmp_flt_oeq:
  case Opcode::cjmp_flt_ogt:
  case Opcode::cjmp_flt_oge:
  case Opcode::cjmp_flt_olt:
  case Opcode::cjmp_flt_ole:
  case Opcode::cjmp_flt_one:
  case Opcode::cjmp_flt_ord:
  case Opcode::cjmp_flt_uno:
  case Opcode::cjmp_flt_ueq:
  case Opcode::cjmp_flt_ugt:
  case Opcode::cjmp_flt_uge:
  case Opcode::cjmp_flt_ult:
  case Opcode::cjmp_flt_ule:
  case Opcode::cjmp_flt_une:
  case Opcode::cjmp:
  case Opcode::jmp:
  case Opcode::ret:
    return;
  case Opcode::arg_setup:
  case Opcode::invoke:
    if (instr.n_args > 1) {
      out.push_back(instr.args[1]);
    }
    return;
  }
}

// TODO: could also return len and take a Margument[4] as arg
void read_args(const MInstr &instr, TVec<MArgument> &out) {
  switch (instr.op) {
  case Opcode::pop:
  case Opcode::jmp:
    return;
  case Opcode::ret:
    if (instr.n_args > 0) {
      out.push_back(instr.args[0]);
    }
    return;
  case Opcode::call:
  case Opcode::invoke:
  case Opcode::push:
  case Opcode::cjmp:
  case Opcode::arg_setup:
    out.push_back(instr.args[0]);
    return;
  case Opcode::mov_zx:
  case Opcode::mov_sx:
  case Opcode::mov:
  case Opcode::itrunc:
  case Opcode::lea:
  case Opcode::SI2FL:
  case Opcode::UI2FL:
  case Opcode::FL2SI:
  case Opcode::FL2UI:
    out.push_back(instr.args[1]);
    return;
  case Opcode::shl2:
  case Opcode::shr2:
  case Opcode::sar2:
  case Opcode::land2:
  case Opcode::lor2:
  case Opcode::lxor2:
  case Opcode::add2:
  case Opcode::sub2:
  case Opcode::mul2:
  case Opcode::cjmp_int_slt:
  case Opcode::cjmp_int_sge:
  case Opcode::cjmp_int_sle:
  case Opcode::cjmp_int_sgt:
  case Opcode::cjmp_int_ult:
  case Opcode::cjmp_int_ule:
  case Opcode::cjmp_int_ugt:
  case Opcode::cjmp_int_uge:
  case Opcode::cjmp_int_ne:
  case Opcode::cjmp_int_eq:
  case Opcode::cjmp_flt_oeq:
  case Opcode::cjmp_flt_ogt:
  case Opcode::cjmp_flt_oge:
  case Opcode::cjmp_flt_olt:
  case Opcode::cjmp_flt_ole:
  case Opcode::cjmp_flt_one:
  case Opcode::cjmp_flt_ord:
  case Opcode::cjmp_flt_uno:
  case Opcode::cjmp_flt_ueq:
  case Opcode::cjmp_flt_ugt:
  case Opcode::cjmp_flt_uge:
  case Opcode::cjmp_flt_ult:
  case Opcode::cjmp_flt_ule:
  case Opcode::cjmp_flt_une:
    out.push_back(instr.args[0]);
    out.push_back(instr.args[1]);
    return;
  case Opcode::fadd:
  case Opcode::fsub:
  case Opcode::fmul:
  case Opcode::fdiv:
  case Opcode::fxor:
  case Opcode::fAnd:
  case Opcode::fOr:
  case Opcode::icmp_slt:
  case Opcode::icmp_eq:
  case Opcode::icmp_ult:
  case Opcode::icmp_ne:
  case Opcode::icmp_sgt:
  case Opcode::icmp_ugt:
  case Opcode::icmp_uge:
  case Opcode::icmp_ule:
  case Opcode::icmp_sge:
  case Opcode::icmp_sle:
  case Opcode::fcmp_isNaN:
  case Opcode::fcmp_oeq:
  case Opcode::fcmp_ogt:
  case Opcode::fcmp_oge:
  case Opcode::fcmp_olt:
  case Opcode::fcmp_ole:
  case Opcode::fcmp_one:
  case Opcode::fcmp_ord:
  case Opcode::fcmp_uno:
  case Opcode::fcmp_ueq:
  case Opcode::fcmp_ugt:
  case Opcode::fcmp_uge:
  case Opcode::fcmp_ult:
  case Opcode::fcmp_ule:
  case Opcode::fcmp_une:
    out.push_back(instr.args[1]);
    out.push_back(instr.args[2]);
    return;
  case Opcode::cmov:
  case Opcode::ffmadd132:
  case Opcode::ffmadd213:
  case Opcode::ffmadd231:
    out.push_back(instr.args[0]);
    out.push_back(instr.args[1]);
    out.push_back(instr.args[2]);
    return;
  case Opcode::idiv:
    out.push_back(instr.args[2]);
    out.push_back(instr.args[3]);
    return;
  }
}

} // namespace foptim::fmir

fmt::appender
fmt::formatter<foptim::fmir::MFunc>::format(foptim::fmir::MFunc const &func,
                                            format_context &ctx) const {
  auto app = ctx.out();
  app = fmt::format_to(app, "func {}<", func.name);
  const auto &attribs = func.get_attribs();
  for (auto [key, value] : attribs) {
    app = fmt::format_to(app, "{}{}, ", key.c_str(), value);
  }
  app = fmt::format_to(app, ">(");
  auto n_args = func.args.size();
  for (foptim::u32 i = 0; i < n_args; i++) {
    app = fmt::format_to(app, "{}: {}, ", func.args[i], func.arg_tys[i]);
  }
  app = fmt::format_to(app, ")\n");

  for (size_t bb_indx = 0; bb_indx < func.bbs.size(); bb_indx++) {
    app = fmt::format_to(app, "  {}:\n{}", bb_indx, func.bbs[bb_indx]);
  }
  return app;
}
fmt::appender
fmt::formatter<foptim::fmir::MBB>::format(foptim::fmir::MBB const &bb,
                                          format_context &ctx) const {
  auto app = ctx.out();
  for (const auto &instr : bb.instrs) {
    app = fmt::format_to(app, "    {}\n", instr);
  }
  return app;
}

fmt::appender
fmt::formatter<foptim::fmir::MInstr>::format(foptim::fmir::MInstr const &v,
                                             format_context &ctx) const {
  auto app = ctx.out();
  switch (v.op) {
  case foptim::fmir::Opcode::mov:
    return fmt::format_to(app, "{} = {}", v.args[0], v.args[1]);
  case foptim::fmir::Opcode::add2:
    return fmt::format_to(app, "{} += {}", v.args[0], v.args[1]);
  case foptim::fmir::Opcode::sub2:
    return fmt::format_to(app, "{} -= {}", v.args[0], v.args[1]);
  case foptim::fmir::Opcode::mul2:
    return fmt::format_to(app, "{} *= {}", v.args[0], v.args[1]);
  case foptim::fmir::Opcode::lxor2:
    if (v.args[0] == v.args[1]) {
      return fmt::format_to(app, "clear {}", v.args[0]);
    }
    return fmt::format_to(app, "{} ^= {}", v.args[0], v.args[1]);
  case foptim::fmir::Opcode::fsub:
    return fmt::format_to(app, "{} = {} - {}", v.args[0], v.args[1], v.args[2]);
  case foptim::fmir::Opcode::fadd:
    return fmt::format_to(app, "{} = {} + {}", v.args[0], v.args[1], v.args[2]);
  case foptim::fmir::Opcode::fmul:
    return fmt::format_to(app, "{} = {} * {}", v.args[0], v.args[1], v.args[2]);
  case foptim::fmir::Opcode::fdiv:
    return fmt::format_to(app, "{} = {} / {}", v.args[0], v.args[1], v.args[2]);
  default:
    app = fmt::format_to(app, "{}(", getNameFromOpcode(v.op));
    for (size_t arg_indx = 0; arg_indx < v.n_args; arg_indx++) {
      app = fmt::format_to(app, "{}, ", v.args[arg_indx]);
    }
    app = fmt::format_to(app, ")");
    if (v.has_bb_ref) {
      app = fmt::format_to(app, " -> {}", v.bb_ref);
    }
    return app;
  }
}
fmt::appender fmt::formatter<foptim::fmir::MArgument>::format(
    foptim::fmir::MArgument const &value, format_context &ctx) const {

  auto app = ctx.out();
  switch (value.type) {
  case foptim::fmir::MArgument::ArgumentType::MemLabel:
    return fmt::format_to(app, fg(fmt::color::blue), "[{}]: {}", value.label,
                          value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmLabel:
    return fmt::format_to(app, fg(fmt::color::blue), "[{} + {}]: {}",
                          value.label, value.imm, value.ty);
  case foptim::fmir::MArgument::ArgumentType::Label:
    return fmt::format_to(app, fg(fmt::color::blue), "{}", value.label);
  case foptim::fmir::MArgument::ArgumentType::Imm: {
    if (value.ty == foptim::fmir::Type::Float32) {
      return fmt::format_to(app, fg(fmt::color::blue), "{}f", value.immf);
    } else if (value.ty == foptim::fmir::Type::Float64) {
      return fmt::format_to(app, fg(fmt::color::blue), "{}d", value.immf);
    } else {
      return fmt::format_to(app, fg(fmt::color::blue), "{}:{}", value.imm,
                            value.ty);
    }
  }
  case foptim::fmir::MArgument::ArgumentType::VReg:
    return fmt::format_to(app, fg(fmt::color::blue), "{}:{}", value.reg,
                          value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemVReg:
    return fmt::format_to(app, fg(fmt::color::blue), "[{}]:{}", value.reg,
                          value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemVRegVReg:
    return fmt::format_to(app, fg(fmt::color::blue), "[{} + {}]:{}", value.reg,
                          value.indx, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImm:
    return fmt::format_to(app, fg(fmt::color::blue), "[{}]:{}", value.imm,
                          value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmVReg:
    return fmt::format_to(app, fg(fmt::color::blue), "[{} + {}]:{}", value.reg,
                          value.imm, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmVRegVReg:
    return fmt::format_to(app, fg(fmt::color::blue), "[{} + {} + {}]:{}",
                          value.reg, value.indx, value.imm, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmVRegScale:
    return fmt::format_to(app, fg(fmt::color::blue), "[{}*{} + {}]:{}",
                          value.indx, value.scale, value.imm, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemVRegVRegScale:
    return fmt::format_to(app, fg(fmt::color::blue), "[{} + {}*{}]:{}",
                          value.reg, value.indx, value.scale, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
    return fmt::format_to(app, fg(fmt::color::blue), "[{} + {}*{} + {}]:{}",
                          value.reg, value.indx, value.scale, value.imm,
                          value.ty);
  }
}

fmt::appender
fmt::formatter<foptim::fmir::Type>::format(foptim::fmir::Type const &v,
                                           format_context &ctx) const {
  auto app = ctx.out();
  switch (v) {
  case foptim::fmir::Type::INVALID:
    return fmt::format_to(app, "INVALID");
  case foptim::fmir::Type::Int8:
    return fmt::format_to(app, "i8");
  case foptim::fmir::Type::Int16:
    return fmt::format_to(app, "i16");
  case foptim::fmir::Type::Int32:
    return fmt::format_to(app, "i32");
  case foptim::fmir::Type::Int64:
    return fmt::format_to(app, "i64");
  case foptim::fmir::Type::Float32:
    return fmt::format_to(app, "f32");
  case foptim::fmir::Type::Float64:
    return fmt::format_to(app, "f64");
  }
}

fmt::appender
fmt::formatter<foptim::fmir::VReg>::format(foptim::fmir::VReg const &value,
                                           format_context &ctx) const {

  using foptim::fmir::VRegType;
  auto app = ctx.out();

  if (value.info.reg_size == 0) {
    switch (value.info.ty) {
    case VRegType::A:
      return fmt::format_to(app, "$a");
    case VRegType::SP:
      return fmt::format_to(app, "$sp");
    case VRegType::B:
      return fmt::format_to(app, "$b");
    case VRegType::C:
      return fmt::format_to(app, "$c");
    case VRegType::D:
      return fmt::format_to(app, "$d");
    case VRegType::DI:
      return fmt::format_to(app, "$di");
    case VRegType::SI:
      return fmt::format_to(app, "$si");
    case VRegType::BP:
      return fmt::format_to(app, "$bp");
    case VRegType::R8:
      return fmt::format_to(app, "$8");
    case VRegType::R9:
      return fmt::format_to(app, "$9");
    case VRegType::R10:
      return fmt::format_to(app, "$10");
    case VRegType::R11:
      return fmt::format_to(app, "$11");
    case VRegType::R12:
      return fmt::format_to(app, "$12");
    case VRegType::R13:
      return fmt::format_to(app, "$13");
    case VRegType::R14:
      return fmt::format_to(app, "$14");
    case VRegType::R15:
      return fmt::format_to(app, "$15");
    default:
    }
  } else if (value.info.reg_size == 1) {
    switch (value.info.ty) {
    case VRegType::A:
      return fmt::format_to(app, "$al");
    case VRegType::SP:
      return fmt::format_to(app, "$spl");
    case VRegType::B:
      return fmt::format_to(app, "$bl");
    case VRegType::C:
      return fmt::format_to(app, "$cl");
    case VRegType::D:
      return fmt::format_to(app, "$dl");
    case VRegType::DI:
      return fmt::format_to(app, "$ldi");
    case VRegType::SI:
      return fmt::format_to(app, "$lsi");
    case VRegType::BP:
      return fmt::format_to(app, "$lbp");
    case VRegType::R8:
      return fmt::format_to(app, "$r8l");
    case VRegType::R9:
      return fmt::format_to(app, "$r9l");
    case VRegType::R10:
      return fmt::format_to(app, "$r10l");
    case VRegType::R11:
      return fmt::format_to(app, "$r11l");
    case VRegType::R12:
      return fmt::format_to(app, "$r12l");
    case VRegType::R13:
      return fmt::format_to(app, "$r13l");
    case VRegType::R14:
      return fmt::format_to(app, "$r14l");
    case VRegType::R15:
      return fmt::format_to(app, "$r15l");
    default:
    }
  } else if (value.info.reg_size == 2) {
    switch (value.info.ty) {
    case VRegType::A:
      return fmt::format_to(app, "$ax");
    case VRegType::SP:
      return fmt::format_to(app, "$sp");
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
      return fmt::format_to(app, "$$2$$");
    default:
    }
  } else if (value.info.reg_size == 4) {
    switch (value.info.ty) {
    case VRegType::A:
      return fmt::format_to(app, "$eax");
    case VRegType::SP:
      return fmt::format_to(app, "$esp");
    case VRegType::B:
      return fmt::format_to(app, "$ebx");
    case VRegType::C:
      return fmt::format_to(app, "$ecx");
    case VRegType::D:
      return fmt::format_to(app, "$edx");
    case VRegType::DI:
      return fmt::format_to(app, "$edi");
    case VRegType::SI:
      return fmt::format_to(app, "$esi");
    case VRegType::BP:
      return fmt::format_to(app, "$ebp");
    case VRegType::R8:
      return fmt::format_to(app, "$r8d");
    case VRegType::R9:
      return fmt::format_to(app, "$r9d");
    case VRegType::R10:
      return fmt::format_to(app, "$r10d");
    case VRegType::R11:
      return fmt::format_to(app, "$r11d");
    case VRegType::R12:
      return fmt::format_to(app, "$r12d");
    case VRegType::R13:
      return fmt::format_to(app, "$r13d");
    case VRegType::R14:
      return fmt::format_to(app, "$r14d");
    case VRegType::R15:
      return fmt::format_to(app, "$r15d");
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
      return fmt::format_to(
          app, "$mm{}",
          ((foptim::u8)value.info.ty - (foptim::u8)VRegType::mm0));
    default:
    }
  } else if (value.info.reg_size == 8) {
    switch (value.info.ty) {
    case VRegType::A:
      return fmt::format_to(app, "$rax");
    case VRegType::SP:
      return fmt::format_to(app, "$rsp");
    case VRegType::B:
      return fmt::format_to(app, "$rbx");
    case VRegType::C:
      return fmt::format_to(app, "$rcx");
    case VRegType::D:
      return fmt::format_to(app, "$rdx");
    case VRegType::DI:
      return fmt::format_to(app, "$rdi");
    case VRegType::SI:
      return fmt::format_to(app, "$rsi");
    case VRegType::BP:
      return fmt::format_to(app, "$rbp");
    case VRegType::R8:
      return fmt::format_to(app, "$r8");
    case VRegType::R9:
      return fmt::format_to(app, "$r9");
    case VRegType::R10:
      return fmt::format_to(app, "$r10");
    case VRegType::R11:
      return fmt::format_to(app, "$r11");
    case VRegType::R12:
      return fmt::format_to(app, "$r12");
    case VRegType::R13:
      return fmt::format_to(app, "$r13");
    case VRegType::R14:
      return fmt::format_to(app, "$r14");
    case VRegType::R15:
      return fmt::format_to(app, "$r15");
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
      return fmt::format_to(
          app, "$mm{}",
          ((foptim::u8)value.info.ty - (foptim::u8)VRegType::mm0));
    default:
    }
  }
  return fmt::format_to(app, "${}", value.id);
}
