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
    return fmt::format_to(app, "[{}]: {}", value.label, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmLabel:
    return fmt::format_to(app, "[{} + {}]: {}", value.label, value.imm,
                          value.ty);
  case foptim::fmir::MArgument::ArgumentType::Label:
    return fmt::format_to(app, "{}", value.label);
  case foptim::fmir::MArgument::ArgumentType::Imm: {
    if (value.ty == foptim::fmir::Type::Float32) {
      return fmt::format_to(app, fg(fmt::color::cadet_blue), "{}f", value.immf);
    } else if (value.ty == foptim::fmir::Type::Float64) {
      return fmt::format_to(app, fg(fmt::color::cadet_blue), "{}d", value.immf);
    } else {
      return fmt::format_to(app, fg(fmt::color::cadet_blue), "{}:{}", value.imm,
                            value.ty);
    }
  }
  case foptim::fmir::MArgument::ArgumentType::VReg:
    return fmt::format_to(app, "{}:{}", value.reg, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemVReg:
    return fmt::format_to(app, "[{}]:{}", value.reg, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemVRegVReg:
    return fmt::format_to(app, "[{} + {}]:{}", value.reg, value.indx, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImm:
    return fmt::format_to(app, "[{}]:{}", value.imm, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmVReg:
    return fmt::format_to(app, "[{} + {}]:{}", value.reg, value.imm, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmVRegVReg:
    return fmt::format_to(app, "[{} + {} + {}]:{}", value.reg, value.indx,
                          value.imm, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmVRegScale:
    return fmt::format_to(app, "[{}*{} + {}]:{}", value.indx, value.scale,
                          value.imm, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemVRegVRegScale:
    return fmt::format_to(app, "[{} + {}*{}]:{}", value.reg, value.indx,
                          value.scale, value.ty);
  case foptim::fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
    return fmt::format_to(app, "[{} + {}*{} + {}]:{}", value.reg, value.indx,
                          value.scale, value.imm, value.ty);
  }
}

fmt::appender
fmt::formatter<foptim::fmir::Type>::format(foptim::fmir::Type const &v,
                                           format_context &ctx) const {
  auto app = ctx.out();
  constexpr auto col = fg(fmt::color::light_coral);
  switch (v) {
  case foptim::fmir::Type::INVALID:
    return fmt::format_to(app, col, "INVALID");
  case foptim::fmir::Type::Int8:
    return fmt::format_to(app, col, "i8");
  case foptim::fmir::Type::Int16:
    return fmt::format_to(app, col, "i16");
  case foptim::fmir::Type::Int32:
    return fmt::format_to(app, col, "i32");
  case foptim::fmir::Type::Int64:
    return fmt::format_to(app, col, "i64");
  case foptim::fmir::Type::Float32:
    return fmt::format_to(app, col, "f32");
  case foptim::fmir::Type::Float64:
    return fmt::format_to(app, col, "f64");
  }
}

fmt::appender
fmt::formatter<foptim::fmir::VReg>::format(foptim::fmir::VReg const &value,
                                           format_context &ctx) const {

  using foptim::fmir::CReg;
  auto app = ctx.out();
  auto col = fg(fmt::color::light_sky_blue);
  auto col_vec = fg(fmt::color::steel_blue);
  if (!value.is_concrete()) {
    return fmt::format_to(app, col, "${}", value.virt_id());
  }

  if (get_size(value.ty) == 0) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, col, "$a");
    case CReg::SP:
      return fmt::format_to(app, col, "$sp");
    case CReg::B:
      return fmt::format_to(app, col, "$b");
    case CReg::C:
      return fmt::format_to(app, col, "$c");
    case CReg::D:
      return fmt::format_to(app, col, "$d");
    case CReg::DI:
      return fmt::format_to(app, col, "$di");
    case CReg::SI:
      return fmt::format_to(app, col, "$si");
    case CReg::BP:
      return fmt::format_to(app, col, "$bp");
    case CReg::R8:
      return fmt::format_to(app, col, "$8");
    case CReg::R9:
      return fmt::format_to(app, col, "$9");
    case CReg::R10:
      return fmt::format_to(app, col, "$10");
    case CReg::R11:
      return fmt::format_to(app, col, "$11");
    case CReg::R12:
      return fmt::format_to(app, col, "$12");
    case CReg::R13:
      return fmt::format_to(app, col, "$13");
    case CReg::R14:
      return fmt::format_to(app, col, "$14");
    case CReg::R15:
      return fmt::format_to(app, col, "$15");
    default:
    }
  } else if (get_size(value.ty) == 1) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, col, "$al");
    case CReg::SP:
      return fmt::format_to(app, col, "$spl");
    case CReg::B:
      return fmt::format_to(app, col, "$bl");
    case CReg::C:
      return fmt::format_to(app, col, "$cl");
    case CReg::D:
      return fmt::format_to(app, col, "$dl");
    case CReg::DI:
      return fmt::format_to(app, col, "$ldi");
    case CReg::SI:
      return fmt::format_to(app, col, "$lsi");
    case CReg::BP:
      return fmt::format_to(app, col, "$lbp");
    case CReg::R8:
      return fmt::format_to(app, col, "$r8l");
    case CReg::R9:
      return fmt::format_to(app, col, "$r9l");
    case CReg::R10:
      return fmt::format_to(app, col, "$r10l");
    case CReg::R11:
      return fmt::format_to(app, col, "$r11l");
    case CReg::R12:
      return fmt::format_to(app, col, "$r12l");
    case CReg::R13:
      return fmt::format_to(app, col, "$r13l");
    case CReg::R14:
      return fmt::format_to(app, col, "$r14l");
    case CReg::R15:
      return fmt::format_to(app, col, "$r15l");
    default:
    }
  } else if (get_size(value.ty) == 2) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, col, "$ax");
    case CReg::SP:
      return fmt::format_to(app, col, "$sp");
    case CReg::B:
    case CReg::C:
    case CReg::D:
    case CReg::DI:
    case CReg::SI:
    case CReg::BP:
    case CReg::R8:
    case CReg::R9:
    case CReg::R10:
    case CReg::R11:
    case CReg::R12:
    case CReg::R13:
    case CReg::R14:
    case CReg::R15:
      return fmt::format_to(app, col_vec, "$$2$$");
    default:
    }
  } else if (get_size(value.ty) == 4) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, col, "$eax");
    case CReg::SP:
      return fmt::format_to(app, col, "$esp");
    case CReg::B:
      return fmt::format_to(app, col, "$ebx");
    case CReg::C:
      return fmt::format_to(app, col, "$ecx");
    case CReg::D:
      return fmt::format_to(app, col, "$edx");
    case CReg::DI:
      return fmt::format_to(app, col, "$edi");
    case CReg::SI:
      return fmt::format_to(app, col, "$esi");
    case CReg::BP:
      return fmt::format_to(app, col, "$ebp");
    case CReg::R8:
      return fmt::format_to(app, col, "$r8d");
    case CReg::R9:
      return fmt::format_to(app, col, "$r9d");
    case CReg::R10:
      return fmt::format_to(app, col, "$r10d");
    case CReg::R11:
      return fmt::format_to(app, col, "$r11d");
    case CReg::R12:
      return fmt::format_to(app, col, "$r12d");
    case CReg::R13:
      return fmt::format_to(app, col, "$r13d");
    case CReg::R14:
      return fmt::format_to(app, col, "$r14d");
    case CReg::R15:
      return fmt::format_to(app, col, "$r15d");
    case CReg::mm0:
    case CReg::mm1:
    case CReg::mm2:
    case CReg::mm3:
    case CReg::mm4:
    case CReg::mm5:
    case CReg::mm6:
    case CReg::mm7:
    case CReg::mm8:
    case CReg::mm9:
    case CReg::mm10:
    case CReg::mm11:
    case CReg::mm12:
    case CReg::mm13:
    case CReg::mm14:
    case CReg::mm15:
      return fmt::format_to(
          app, col_vec, "$mm{}",
          ((foptim::u8)value.c_reg() - (foptim::u8)CReg::mm0));
    default:
    }
  } else if (get_size(value.ty) == 8) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, col, "$rax");
    case CReg::SP:
      return fmt::format_to(app, col, "$rsp");
    case CReg::B:
      return fmt::format_to(app, col, "$rbx");
    case CReg::C:
      return fmt::format_to(app, col, "$rcx");
    case CReg::D:
      return fmt::format_to(app, col, "$rdx");
    case CReg::DI:
      return fmt::format_to(app, col, "$rdi");
    case CReg::SI:
      return fmt::format_to(app, col, "$rsi");
    case CReg::BP:
      return fmt::format_to(app, col, "$rbp");
    case CReg::R8:
      return fmt::format_to(app, col, "$r8");
    case CReg::R9:
      return fmt::format_to(app, col, "$r9");
    case CReg::R10:
      return fmt::format_to(app, col, "$r10");
    case CReg::R11:
      return fmt::format_to(app, col, "$r11");
    case CReg::R12:
      return fmt::format_to(app, col, "$r12");
    case CReg::R13:
      return fmt::format_to(app, col, "$r13");
    case CReg::R14:
      return fmt::format_to(app, col, "$r14");
    case CReg::R15:
      return fmt::format_to(app, col, "$r15");
    case CReg::mm0:
    case CReg::mm1:
    case CReg::mm2:
    case CReg::mm3:
    case CReg::mm4:
    case CReg::mm5:
    case CReg::mm6:
    case CReg::mm7:
    case CReg::mm8:
    case CReg::mm9:
    case CReg::mm10:
    case CReg::mm11:
    case CReg::mm12:
    case CReg::mm13:
    case CReg::mm14:
    case CReg::mm15:
      return fmt::format_to(app, col_vec, "$mm{}",
                            ((foptim::u8)value.c_reg() - (foptim::u8)CReg::mm0));
    default:
    }
  } else {
    TODO("unrach?");
  }
  return fmt::format_to(app, "REG PRINT FAIL TYPE:{} OFF:{}", value.ty, (foptim::u8)value.c_reg());
}
