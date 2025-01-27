#include "backend.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "utils/arena.hpp"
#include <elfio/elf_types.hpp>
#include <elfio/elfio.hpp>
#include <fadec-enc.h>
#include <fadec.h>
#include <memory>

namespace foptim::codegen {

enum class RelocSection : u8 {
  INVALID,
  Data,
  Text,
};

struct LabelRelocData {
  struct Usage {
    // absolute location *inside* of the section
    u64 usage_loc;
    RelocSection usage_section = RelocSection::INVALID;
  };

  // absolute location *inside* of the section
  u64 def_loc = 0;
  RelocSection section = RelocSection::INVALID;
  u64 size = 0;
  TVec<Usage> usage_loc;
};

struct BBRelocData {
  u64 def_loc = 0;
  TVec<std::pair<u64, u64>> usage_loc;
};

struct TLabelUsageMap {
  TMap<IRString, LabelRelocData> label_map;
  TMap<u32, BBRelocData> bb_map;

  void insert_label_ref(const IRString &label, u64 loc, RelocSection section) {
    label_map[label].usage_loc.push_back({loc, section});
  }

  void insert_bb_ref(u32 bb_id, u64 memonic, u64 loc) {
    bb_map[bb_id].usage_loc.emplace_back(memonic, loc);
  }
};

template <class OutType = i64> OutType convert_imm(fmir::MArgument const &arg) {
  // utils::Debug << " imm arg:" << arg << "\n";
  assert(arg.isImm());

  if (arg.is_fp()) {
    TODO("impl fp");
  }
  return (OutType)arg.imm;
}

FeOp convert_mem(fmir::MArgument const &arg, TLabelUsageMap &label_usages,
                 u64 curr_off) {
  utils::Debug << " mem arg: " << arg << "\n";
  assert(arg.isMem());

  switch (arg.type) {
  case fmir::MArgument::ArgumentType::Imm:
  case fmir::MArgument::ArgumentType::VReg:
  case fmir::MArgument::ArgumentType::Label:
    UNREACH();
  case fmir::MArgument::ArgumentType::MemLabel:
    // TODO: figure out which section its from
    label_usages.insert_label_ref(arg.label, curr_off, RelocSection::Text);
  case fmir::MArgument::ArgumentType::MemVReg:
  case fmir::MArgument::ArgumentType::MemVRegVReg:
  case fmir::MArgument::ArgumentType::MemImm:
  case fmir::MArgument::ArgumentType::MemImmVReg:
  case fmir::MArgument::ArgumentType::MemImmVRegVReg:
  case fmir::MArgument::ArgumentType::MemVRegVRegScale:
  case fmir::MArgument::ArgumentType::MemImmVRegScale:
  case fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
  case fmir::MArgument::ArgumentType::MemImmLabel:
    utils::Debug << " mem_arg: " << arg << "\n";
    TODO("okak");
  }
  (void)arg;
  UNREACH();
}

FeReg convert_reg(fmir::MArgument const &arg) {
  // utils::Debug << " reg arg: " << arg << "\n";
  assert(arg.isReg());
  auto r = arg.reg;
  switch (r.info.ty) {
  case fmir::VRegType::Virtual:
    //"No virtual reg allowed here"
    UNREACH();
  case fmir::VRegType::A:
    return FE_AX;
  case fmir::VRegType::B:
    return FE_BX;
  case fmir::VRegType::C:
    return FE_CX;
  case fmir::VRegType::D:
    return FE_DX;
  case fmir::VRegType::SI:
    return FE_SI;
  case fmir::VRegType::DI:
    return FE_DI;
  case fmir::VRegType::SP:
    return FE_SP;
  case fmir::VRegType::BP:
    return FE_BP;
  case fmir::VRegType::R8:
    return FE_R8;
  case fmir::VRegType::R9:
    return FE_R9;
  case fmir::VRegType::R10:
    return FE_R10;
  case fmir::VRegType::R11:
    return FE_R11;
  case fmir::VRegType::R12:
    return FE_R12;
  case fmir::VRegType::R13:
    return FE_R13;
  case fmir::VRegType::R14:
    return FE_R14;
  case fmir::VRegType::R15:
    return FE_R15;
  case fmir::VRegType::mm0:
  case fmir::VRegType::mm1:
  case fmir::VRegType::mm2:
  case fmir::VRegType::mm3:
  case fmir::VRegType::mm4:
  case fmir::VRegType::mm5:
  case fmir::VRegType::mm6:
  case fmir::VRegType::mm7:
  case fmir::VRegType::mm8:
  case fmir::VRegType::mm9:
  case fmir::VRegType::mm10:
  case fmir::VRegType::mm11:
  case fmir::VRegType::mm12:
  case fmir::VRegType::mm13:
  case fmir::VRegType::mm14:
  case fmir::VRegType::mm15:
  case fmir::VRegType::N_REGS:
    UNREACH();
  }
}

FeOp convert_arg(fmir::MArgument const &arg, TLabelUsageMap &label_usages,
                 u64 curr_off) {
  if (arg.isImm()) {
    return convert_imm(arg);
  }
  if (arg.isReg()) {
    return convert_reg(arg);
  }
  if (arg.isMem()) {
    return convert_mem(arg, label_usages, curr_off);
  }
  utils::Debug << " arg: " << arg << "\n";
  IMPL("dont think i need this");
}

[[noreturn]] u64 undef_instr2(fmir::MArgument a1, fmir::MArgument a2,
                              const char *filename, u64 line) {
  utils::Debug << "Args were: " << a1 << " and " << a2 << "\n";
  foptim::todo_impl("UNDEF INSTR\n", filename, line);
}

#define UNDEF_INSTR undef_instr2(a1, a2, __FILE__, __LINE__)

#define FE_CMOVZ8ri UNDEF_INSTR
#define FE_CMOVZ16ri UNDEF_INSTR
#define FE_CMOVZ32ri UNDEF_INSTR
#define FE_CMOVZ64ri UNDEF_INSTR
#define FE_CMOVZ8rr UNDEF_INSTR
#define FE_CMOVZ8mi UNDEF_INSTR
#define FE_CMOVZ16mi UNDEF_INSTR
#define FE_CMOVZ32mi UNDEF_INSTR
#define FE_CMOVZ64mi UNDEF_INSTR
#define FE_CMOVZ8mr UNDEF_INSTR
#define FE_CMOVZ16mr UNDEF_INSTR
#define FE_CMOVZ32mr UNDEF_INSTR
#define FE_CMOVZ64mr UNDEF_INSTR

#define genGetMem2(mem)                                                        \
  u64 getMem2##mem(fmir::MArgument a1, fmir::MArgument a2) {                   \
    switch ((a1).type) {                                                       \
    case fmir::MArgument::ArgumentType::Imm:                                   \
    case fmir::MArgument::ArgumentType::Label:                                 \
      UNREACH();                                                               \
    case fmir::MArgument::ArgumentType::VReg:                                  \
      switch ((a2).type) {                                                     \
      case fmir::MArgument::ArgumentType::Imm:                                 \
      case fmir::MArgument::ArgumentType::Label:                               \
        switch ((a1).ty) {                                                     \
        case fmir::Type::Int8:                                                 \
          return FE_##mem##8ri;                                                \
        case fmir::Type::Int16:                                                \
          return FE_##mem##16ri;                                               \
        case fmir::Type::Int32:                                                \
          return FE_##mem##32ri;                                               \
        case fmir::Type::Int64:                                                \
          return FE_##mem##64ri;                                               \
        case fmir::Type::Float32:                                              \
        case fmir::Type::Float64:                                              \
        case fmir::Type::INVALID:                                              \
          UNREACH();                                                           \
        }                                                                      \
      case fmir::MArgument::ArgumentType::VReg:                                \
      default:                                                                 \
        switch (a1.ty) {                                                       \
        case fmir::Type::Int8:                                                 \
          return FE_##mem##8rr;                                                \
        case fmir::Type::Int16:                                                \
          return FE_##mem##16rr;                                               \
        case fmir::Type::Int32:                                                \
          return FE_##mem##32rr;                                               \
        case fmir::Type::Int64:                                                \
          return FE_##mem##64rr;                                               \
        case fmir::Type::Float32:                                              \
        case fmir::Type::Float64:                                              \
        case fmir::Type::INVALID:                                              \
          UNREACH();                                                           \
        }                                                                      \
      }                                                                        \
    default:                                                                   \
      switch (a2.type) {                                                       \
      case fmir::MArgument::ArgumentType::Imm:                                 \
      case fmir::MArgument::ArgumentType::Label:                               \
        switch (a1.ty) {                                                       \
        case fmir::Type::Int8:                                                 \
          return FE_##mem##8mi;                                                \
        case fmir::Type::Int16:                                                \
          return FE_##mem##16mi;                                               \
        case fmir::Type::Int32:                                                \
          return FE_##mem##32mi;                                               \
        case fmir::Type::Int64:                                                \
          return FE_##mem##64mi;                                               \
        case fmir::Type::Float32:                                              \
        case fmir::Type::Float64:                                              \
        case fmir::Type::INVALID:                                              \
          UNREACH();                                                           \
        }                                                                      \
      case fmir::MArgument::ArgumentType::VReg:                                \
      default:                                                                 \
        switch (a1.ty) {                                                       \
        case fmir::Type::Int8:                                                 \
          return FE_##mem##8mr;                                                \
        case fmir::Type::Int16:                                                \
          return FE_##mem##16mr;                                               \
        case fmir::Type::Int32:                                                \
          return FE_##mem##32mr;                                               \
        case fmir::Type::Int64:                                                \
          return FE_##mem##64mr;                                               \
        case fmir::Type::Float32:                                              \
        case fmir::Type::Float64:                                              \
        case fmir::Type::INVALID:                                              \
          UNREACH();                                                           \
        }                                                                      \
      }                                                                        \
    }                                                                          \
  }

// #define genGetMem1RM(mem)                                                      \
//   u64 getMem1##mem(fmir::MArgument a1) {                                  \
//     switch ((a1).type) {                                                       \
//     case fmir::MArgument::ArgumentType::Imm:                                   \
//     case fmir::MArgument::ArgumentType::Label:                                 \
//       switch ((a1).ty) {                                                       \
//       case fmir::Type::Int8:                                                   \
//         return FE_##mem##8i;                                                   \
//       case fmir::Type::Int16:                                                  \
//         return FE_##mem##16i;                                                  \
//       case fmir::Type::Int32:                                                  \
//         return FE_##mem##32i;                                                  \
//       case fmir::Type::Int64:                                                  \
//         return FE_##mem##64i;                                                  \
//       case fmir::Type::Float32:                                                \
//       case fmir::Type::Float64:                                                \
//       case fmir::Type::INVALID:                                                \
//         UNREACH();                                                             \
//       }                                                                        \
//     case fmir::MArgument::ArgumentType::VReg:                                  \
//       switch ((a1).ty) {                                                       \
//       case fmir::Type::Int8:                                                   \
//         return FE_##mem##8r;                                                   \
//       case fmir::Type::Int16:                                                  \
//         return FE_##mem##16r;                                                  \
//       case fmir::Type::Int32:                                                  \
//         return FE_##mem##32r;                                                  \
//       case fmir::Type::Int64:                                                  \
//         return FE_##mem##64r;                                                  \
//       case fmir::Type::Float32:                                                \
//       case fmir::Type::Float64:                                                \
//       case fmir::Type::INVALID:                                                \
//         UNREACH();                                                             \
//       }                                                                        \
//     default:                                                                   \
//       switch (a1.ty) {                                                         \
//       case fmir::Type::Int8:                                                   \
//         return FE_##mem##8m;                                                   \
//       case fmir::Type::Int16:                                                  \
//         return FE_##mem##16m;                                                   \
//       case fmir::Type::Int32:                                                  \
//         return FE_##mem##32m;                                                   \
//       case fmir::Type::Int64:                                                  \
//         return FE_##mem##64m;                                                   \
//       case fmir::Type::Float32:                                                \
//       case fmir::Type::Float64:                                                \
//       case fmir::Type::INVALID:                                                \
//         UNREACH();                                                             \
//       }                                                                        \
//     }                                                                          \
//   }
genGetMem2(MOV);
genGetMem2(AND);
genGetMem2(ADD);
genGetMem2(SUB);
genGetMem2(CMP);
genGetMem2(XOR);
genGetMem2(OR);
genGetMem2(CMOVZ);

// TODO: THIS IS INSANELY DUMB BUT UNLESS THE LiBRARY EXPOSES THIS I GUEsS THIS
// WORKS
u8 *get_arg_offset(u8 *buffer, u8 n_args, u8 arg_id) {
  FdInstr instr;
  int ret = fd_decode(buffer, 16, 64, 0, &instr);
  ASSERT(ret > 0);
  ASSERT(ret == FD_SIZE(&instr));

  FD_TYPE(&instr);
  FD_TYPE(&instr);
  TODO("okak");
  u8 instr_size = ret;
  u8 opcode_size = instr_size;
  u8 op_sizes[4] = {0, 0, 0, 0};
  ASSERT(n_args <= 4);
  for (auto i = 0; i < n_args; i++) {
    auto op_size = FD_OP_SIZE(&instr, i);
    opcode_size -= op_size;
    op_sizes[i] = op_size;
  }

  u8 offset = opcode_size;
  for (auto i = 0; i < arg_id; i++) {
    offset += op_sizes[i];
  }
  return buffer + offset;
}

u32 emit_instr(u8 **buffptr, fmir::MInstr const &instr,
               TLabelUsageMap &label_usages, const u8 *base_ptr) {
  switch (instr.op) {
  case fmir::Opcode::push:
    return fe_enc64(buffptr, FE_PUSHr, convert_reg(instr.args[0]));
  case fmir::Opcode::pop:
    return fe_enc64(buffptr, FE_POPr, convert_reg(instr.args[0]));
  case fmir::Opcode::jmp:
    assert(instr.has_bb_ref);
    label_usages.insert_bb_ref(instr.bb_ref, FE_JMP | FE_JMPL, (u64)*buffptr);
    return fe_enc64(buffptr, FE_JMP | FE_JMPL, (FeOp)*buffptr);
  case fmir::Opcode::call:
    if (instr.args[0].isLabel()) {
      label_usages.insert_label_ref(instr.args[0].label,
                                    (u64)*buffptr + 1 - (u64)base_ptr,
                                    RelocSection::Text);
      return fe_enc64(buffptr, FE_CALL, (FeOp)*buffptr);
    } else {
      return fe_enc64(buffptr, FE_CALLr, convert_reg(instr.args[0]));
    }
  case fmir::Opcode::ret:
    return fe_enc64(buffptr, FE_RET);
  case fmir::Opcode::mov:
    return fe_enc64(
        buffptr, getMem2MOV(instr.args[0], instr.args[1]),
        convert_arg(instr.args[0], label_usages, get_arg_offset(, , )),
        convert_arg(instr.args[1], label_usages));
  case fmir::Opcode::land2:
    return fe_enc64(buffptr, getMem2AND(instr.args[0], instr.args[1]),
                    convert_arg(instr.args[0], label_usages),
                    convert_arg(instr.args[1], label_usages));
  case fmir::Opcode::add2:
    return fe_enc64(buffptr, getMem2ADD(instr.args[0], instr.args[1]),
                    convert_arg(instr.args[0], label_usages),
                    convert_arg(instr.args[1], label_usages));
  case fmir::Opcode::sub2:
    return fe_enc64(buffptr, getMem2SUB(instr.args[0], instr.args[1]),
                    convert_arg(instr.args[0], label_usages),
                    convert_arg(instr.args[1], label_usages));
  case fmir::Opcode::lor2:
    return fe_enc64(buffptr, getMem2OR(instr.args[0], instr.args[1]),
                    convert_arg(instr.args[0], label_usages),
                    convert_arg(instr.args[1], label_usages));
  case fmir::Opcode::lxor2:
    return fe_enc64(buffptr, getMem2XOR(instr.args[0], instr.args[1]),
                    convert_arg(instr.args[0], label_usages),
                    convert_arg(instr.args[1], label_usages));
  case fmir::Opcode::icmp_eq:
    fe_enc64(buffptr, getMem2CMP(instr.args[1], instr.args[2]),
             convert_arg(instr.args[1], label_usages),
             convert_arg(instr.args[2], label_usages));
    return fe_enc64(buffptr, instr.args[0].isReg() ? FE_SETZ8r : FE_SETZ8m,
                    convert_arg(instr.args[0], label_usages));
  case fmir::Opcode::cmov:
    fe_enc64(buffptr, getMem2CMP(instr.args[1], instr.args[1]),
             convert_arg(instr.args[1], label_usages),
             convert_arg(instr.args[1], label_usages));
    return fe_enc64(buffptr, getMem2CMOVZ(instr.args[0], instr.args[2]),
                    convert_arg(instr.args[0], label_usages),
                    convert_arg(instr.args[2], label_usages));
  case fmir::Opcode::cjmp_int_ult:
    fe_enc64(buffptr, getMem2CMP(instr.args[0], instr.args[1]),
             convert_arg(instr.args[0], label_usages),
             convert_arg(instr.args[1], label_usages));
    assert(instr.has_bb_ref);
    label_usages.insert_bb_ref(instr.bb_ref, FE_JC | FE_JMPL, (u64)*buffptr);
    return fe_enc64(buffptr, FE_JC | FE_JMPL, (FeOp)*buffptr);
  case fmir::Opcode::cjmp_int_ule:
    fe_enc64(buffptr, getMem2CMP(instr.args[0], instr.args[1]),
             convert_arg(instr.args[0], label_usages),
             convert_arg(instr.args[1], label_usages));
    assert(instr.has_bb_ref);
    label_usages.insert_bb_ref(instr.bb_ref, FE_JBE | FE_JMPL, (u64)*buffptr);
    return fe_enc64(buffptr, FE_JBE | FE_JMPL, (FeOp)*buffptr);
  case fmir::Opcode::cjmp:
  case fmir::Opcode::mov_zx:
  case fmir::Opcode::mov_sx:
  case fmir::Opcode::itrunc:
  case fmir::Opcode::lea:
  case fmir::Opcode::shl2:
  case fmir::Opcode::shr2:
  case fmir::Opcode::sar2:
  case fmir::Opcode::mul2:
  case fmir::Opcode::idiv:
  case fmir::Opcode::fadd:
  case fmir::Opcode::fsub:
  case fmir::Opcode::fmul:
  case fmir::Opcode::fdiv:
  case fmir::Opcode::ffmadd132:
  case fmir::Opcode::ffmadd213:
  case fmir::Opcode::ffmadd231:
  case fmir::Opcode::fxor:
  case fmir::Opcode::SI2FL:
  case fmir::Opcode::UI2FL:
  case fmir::Opcode::FL2SI:
  case fmir::Opcode::FL2UI:
  case fmir::Opcode::icmp_slt:
  case fmir::Opcode::icmp_ult:
  case fmir::Opcode::icmp_ne:
  case fmir::Opcode::icmp_sgt:
  case fmir::Opcode::icmp_ugt:
  case fmir::Opcode::icmp_uge:
  case fmir::Opcode::icmp_ule:
  case fmir::Opcode::icmp_sge:
  case fmir::Opcode::icmp_sle:
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
  case fmir::Opcode::cjmp_int_slt:
  case fmir::Opcode::cjmp_int_sge:
  case fmir::Opcode::cjmp_int_sle:
  case fmir::Opcode::cjmp_int_sgt:
  case fmir::Opcode::cjmp_int_ugt:
  case fmir::Opcode::cjmp_int_uge:
  case fmir::Opcode::cjmp_int_ne:
  case fmir::Opcode::cjmp_int_eq:
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
    utils::Debug << instr << "\n";
    TODO("not implemented opcode");
  }
}

u8 *assemble(std::span<const fmir::MFunc> funcs, u8 *output_buffer,
             TLabelUsageMap &label_usages) {

  auto *curr_buf_ptr = output_buffer;

  // inserting all functions prior so we can check later one if a label is a
  // function or is from a data segment
  for (const auto &func : funcs) {
    label_usages.label_map[func.name].section = RelocSection::Text;
  }

  for (const auto &func : funcs) {
    // first we need to align it
    auto off = ((curr_buf_ptr - output_buffer) % 0x10);
    if (off != 0) {
      auto missing_align = 0x10 - off;
      while (missing_align > 0) {
        fe_enc64(&curr_buf_ptr, FE_NOP);
        missing_align--;
      }
    }

    const auto *func_start_buf_ptr = curr_buf_ptr;
    label_usages.label_map[func.name].def_loc = curr_buf_ptr - output_buffer;
    size_t bb_id = 0;

    for (const auto &bb : func.bbs) {
      label_usages.bb_map[bb_id].def_loc = (u64)curr_buf_ptr;
      bb_id++;
      for (const auto &instr : bb.instrs) {
        emit_instr(&curr_buf_ptr, instr, label_usages, output_buffer);
      }
    }
    const auto *func_end_buf_ptr = curr_buf_ptr;
    label_usages.label_map[func.name].size =
        func_end_buf_ptr - func_start_buf_ptr;

    for (auto [bb_id, bb_usage] : label_usages.bb_map) {
      assert(bb_usage.def_loc != 0);
      for (auto [instr_mem, usage_loc] : bb_usage.usage_loc) {

        auto *usage_loc_ptr = (u8 *)usage_loc;
        assert(fe_enc64(&usage_loc_ptr, instr_mem, bb_usage.def_loc) == 0);

        // FdInstr instr;
        // assert(fd_decode((u8 *)usage_loc, 16, 64, 0, &instr) > 0);
        // char fmtbuf[64];
        // fd_format(&instr, fmtbuf, sizeof(fmtbuf));
        // utils::Debug << usageloc << " Usage of bb: " << fmtbuf << "\n";
      }
    }
    label_usages.bb_map.clear();
  }
  return curr_buf_ptr;
}

void generate_obj_file(TLabelUsageMap &label_usage_map, u8 *start_txt,
                       u8 *end_txt, std::span<const IRString> decls,
                       std::span<const fmir::Global> globals) {
  using namespace ELFIO;
  (void)label_usage_map;
  (void)start_txt;
  (void)end_txt;
  (void)decls;
  (void)globals;

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

  section *text_rel_sec = writer.sections.add(".rel.text");
  {
    text_rel_sec->set_type(SHT_RELA);
    text_rel_sec->set_info(text_sec->get_index());
    text_rel_sec->set_addr_align(0x4);
    text_rel_sec->set_entry_size(writer.get_default_entry_size(SHT_RELA));
    text_rel_sec->set_link(sym_sec->get_index());
  }
  relocation_section_accessor text_rela(writer, text_rel_sec);

  section *data_sec = writer.sections.add(".data");
  {
    data_sec->set_type(SHT_PROGBITS);
    data_sec->set_flags(SHF_ALLOC | SHF_WRITE);
    data_sec->set_addr_align(0x4);
    char data[] = {'\x00'};
    data_sec->set_data(data, sizeof(data));
  }

  section *data_rel_sec = writer.sections.add(".rel.data");
  {
    data_rel_sec->set_type(SHT_RELA);
    data_rel_sec->set_info(data_sec->get_index());
    data_rel_sec->set_addr_align(0x4);
    data_rel_sec->set_entry_size(writer.get_default_entry_size(SHT_RELA));
    data_rel_sec->set_link(sym_sec->get_index());
  }
  relocation_section_accessor data_rela(writer, data_rel_sec);

  for (auto [label_name, label_data] : label_usage_map.label_map) {
    ASSERT(label_data.section != RelocSection::INVALID);

    Elf_Half sec_indx = 0;
    Elf_Word symbol_type = STT_FUNC;
    Elf_Word symbol_binding = STB_GLOBAL;
    switch (label_data.section) {
    case RelocSection::INVALID:
      UNREACH();
    case RelocSection::Data:
      sec_indx = data_sec->get_index();
      symbol_type = STT_OBJECT;
      break;
    case RelocSection::Text:
      sec_indx = text_sec->get_index();
      symbol_type = STT_FUNC;
      break;
    }

    auto symbol = syma.add_symbol(stra, label_name.c_str(), label_data.def_loc,
                                  label_data.size, symbol_binding, symbol_type,
                                  0, sec_indx);

    for (auto loc : label_data.usage_loc) {
      ASSERT(loc.usage_section != RelocSection::INVALID);
      switch (loc.usage_section) {
      case RelocSection::INVALID:
        UNREACH();
      case RelocSection::Data:
        data_rela.add_entry(loc.usage_loc, symbol,
                            (unsigned char)R_X86_64_PLT32, -4);
        break;
      case RelocSection::Text:
        text_rela.add_entry(loc.usage_loc, symbol,
                            (unsigned char)R_X86_64_PLT32, -4);
        break;
      }
    }
  }

  section *note_gnu_stack_sec = writer.sections.add(".note.GNU-stack");
  { // section .note.GNU-stack noalloc noexec nowrite progbits
    note_gnu_stack_sec->set_type(SHT_PROGBITS);
    // note_section_accessor note_writer(writer, note_gnu_stack_sec);
  }

  section *note_sec = writer.sections.add(".note");
  {
    note_sec->set_type(SHT_NOTE);
    note_section_accessor note_writer(writer, note_sec);
    note_writer.add_note(0x01, "created by foptim", nullptr, 0);
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

  writer.save("hello.o");
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
  TLabelUsageMap label_usages;
  auto *end_buff_ptr = assemble(funcs, output_buffer, label_usages);

  generate_obj_file(label_usages, output_buffer, end_buff_ptr, decls, globals);

  // utils::Debug << " Generated " << end_buff_ptr - output_buffer <<
  // "bytes\n"; utils::Debug << " Instructions:\n";

  // auto *read_buff = output_buffer;
  // while (read_buff < end_buff_ptr) {
  //   FdInstr instr;
  //   int ret = fd_decode(read_buff, sizeof(read_buff), 64, 0, &instr);
  //   assert(ret > 0);
  //   read_buff += ret;
  //   char fmtbuf[64];
  //   fd_format(&instr, fmtbuf, sizeof(fmtbuf));
  //   utils::Debug << "  " << fmtbuf << "\n";
  // }

  // utils::Debug << " yut\n";
}

} // namespace foptim::codegen
