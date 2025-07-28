#include <fmt/core.h>

#include <cmath>
#include <elfio/elf_types.hpp>
#include <elfio/elfio.hpp>

#include "backend.hpp"
#include "backend3.hpp"
#include "ir/helpers.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "third_party/Zydis.h"
#include "utils/arena.hpp"
#include "utils/parameters.hpp"
#include "x86_codegen/backend3_instr_gen.hpp"

namespace foptim::codegen {

namespace {

size_t emit_instr(const fmir::MInstr &instr, u8 *const out_buff, u8 curr_bb_id,
                  TLabelUsageMap &reloc_map, ProEpilogueType proepiloguetype) {
  // size_t length = 999;
  ZydisEncoderRequest req;
  memset(&req, 0, sizeof(req));
  req.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
  req.operand_count = instr.n_args;
  for (auto i = 0; i < req.operand_count; i++) {
    emit_operand(instr.args[i], req.operands[i], reloc_map, out_buff, i);
  }

  switch (instr.bop) {
    case fmir::GOpcode::GBase:
      return emit_gbase(req, instr, out_buff, curr_bb_id, reloc_map,
                        proepiloguetype);
    case fmir::GOpcode::GJmp:
      return emit_gjmp(req, instr, out_buff, curr_bb_id, reloc_map,
                       proepiloguetype);
    case fmir::GOpcode::GConv:
      return emit_gconv(req, instr, out_buff, curr_bb_id, reloc_map,
                        proepiloguetype);
    case fmir::GOpcode::GArith:
      return emit_garith(req, instr, out_buff, curr_bb_id, reloc_map,
                         proepiloguetype);
    case fmir::GOpcode::GCMov:
      return emit_gcmov(req, instr, out_buff, curr_bb_id, reloc_map,
                        proepiloguetype);
    case fmir::GOpcode::GVec:
      return emit_gvec(req, instr, out_buff, curr_bb_id, reloc_map,
                       proepiloguetype);
    case fmir::GOpcode::X86:
      return emit_x86(req, instr, out_buff, curr_bb_id, reloc_map,
                      proepiloguetype);
      break;
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
  fmt::println("DEBUG: {}", instruction.text);
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

  auto old_val =
      is_imm ? operands[op_num].imm.value.s : operands[op_num].mem.disp.value;
  return {.op_addr = buff + op_off,
          .op_off = (u8)(instruction.length - op_off),
          .op_val = old_val};
}

void reloc_bbs(TLabelUsageMap &reloc_map, u8 *buff_start) {
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

  for (auto &[bb_id, bb_data] : reloc_map.bb_map) {
    auto bb_loc = bb_data.def_loc;

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
  ZoneScopedN("Assembling .text");
  u8 *curr_loc = out_buff;
  for (const auto &func : funcs) {
    // fmt::println("{}", func);
    {  // make sure were aligned
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

    ProEpilogueType proepilogue = ProEpilogueType::None;
    // can only remove epi/pro logue if it doesnt touch any local stack stuff
    // and no cals(cause of alignment could fix that some other way by just
    // dummy pushing?)
    {
      for (const auto &bb : func.bbs) {
        for (const auto &instr : bb.instrs) {
          if (instr.is(fmir::GBaseSubtype::call) ||
              instr.is(fmir::GBaseSubtype::invoke)) {
            proepilogue = std::min(proepilogue, ProEpilogueType::Align);
          }
          for (u8 i = 0; i < instr.n_args; i++) {
            if (instr.args[i].uses_same_vreg(fmir::VReg::RBP()) ||
                instr.args[i].uses_same_vreg(fmir::VReg::RSP())) {
              proepilogue = std::min(proepilogue, ProEpilogueType::Full);
            }
          }
        }
      }
    }

    // prologue
    if (proepilogue == ProEpilogueType::Full) {
      ZydisEncoderRequest req;
      memset(&req, 0, sizeof(req));
      req.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
      req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
      req.operands[1].type = ZYDIS_OPERAND_TYPE_REGISTER;

      req.mnemonic = ZYDIS_MNEMONIC_PUSH;
      req.operand_count = 1;
      req.operands[0].reg.value = ZYDIS_REGISTER_RBP;
      curr_loc += emit(curr_loc, 0, &req);
      req.mnemonic = ZYDIS_MNEMONIC_MOV;
      req.operand_count = 2;
      req.operands[0].reg.value = ZYDIS_REGISTER_RBP;
      req.operands[1].reg.value = ZYDIS_REGISTER_RSP;
      curr_loc += emit(curr_loc, 0, &req);
    } else if (proepilogue == ProEpilogueType::Align) {
      ZydisEncoderRequest req;
      memset(&req, 0, sizeof(req));
      req.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
      req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
      req.operands[1].type = ZYDIS_OPERAND_TYPE_REGISTER;

      // TOOD: idk maybe push smth else
      req.mnemonic = ZYDIS_MNEMONIC_PUSH;
      req.operand_count = 1;
      req.operands[0].reg.value = ZYDIS_REGISTER_RBP;
      curr_loc += emit(curr_loc, 0, &req);
    }

    u64 bb_id = 0;
    for (const auto &bb : func.bbs) {
      reloc_map.bb_map[bb_id].def_loc = curr_loc - out_buff;
      reloc_map.bb_map[bb_id].section = RelocSection::Text;
      reloc_map.bb_map[bb_id].kind = RelocKind::BB;

      for (const auto &instr : bb.instrs) {
        curr_loc += emit_instr(instr, curr_loc, bb_id, reloc_map, proepilogue);
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
  ZoneScopedN("Generating obj");
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

  for (const auto &decl : decls) {
    label_usage_map.label_map[decl].def_loc = 0;
    label_usage_map.label_map[decl].kind = RelocKind::Func;
    label_usage_map.label_map[decl].section = RelocSection::Extern;
    label_usage_map.label_map[decl].size = 0;
  }
  for (auto global : globals) {
    label_usage_map.label_map[global.name];
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

  section *init_array_sec = writer.sections.add(".init_array");
  {
    init_array_sec->set_type(SHT_INIT_ARRAY);
    init_array_sec->set_flags(SHF_ALLOC | SHF_WRITE);
    init_array_sec->set_addr_align(0x8);
    init_array_sec->set_entry_size(8);
    // init_array_sec->set_link(sym_sec->get_index());
  }
  section *init_array_rel_sec = writer.sections.add(".rela.init_array");
  {
    init_array_rel_sec->set_type(SHT_RELA);
    init_array_rel_sec->set_info(init_array_sec->get_index());
    init_array_rel_sec->set_addr_align(0x8);
    init_array_rel_sec->set_entry_size(writer.get_default_entry_size(SHT_RELA));
    init_array_rel_sec->set_link(sym_sec->get_index());
  }
  relocation_section_accessor init_array_rela(writer, init_array_rel_sec);

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
    for (const auto &global : globals) {
      if (strcmp(global.name, "llvm.global_ctors") == 0) {
        label_usage_map.label_map[global.name].def_loc = 0;
        label_usage_map.label_map[global.name].kind = RelocKind::Data;
        label_usage_map.label_map[global.name].section =
            RelocSection::InitArray;
        label_usage_map.label_map[global.name].size = global.data.size();
        label_usage_map.label_map[global.name].vis = global.vis;
        // fmt::println("CTOR {}", global.name);
        {  // handle reloccs
          int i = 0;
          for (const auto &reloc_info : global.reloc_info) {
            ASSERT(8 + i * 24ULL == (uint64_t)reloc_info.insert_offset);
            label_usage_map.label_map[reloc_info.name].usage_loc.push_back(
                LabelRelocData::Usage{
                    .usage_instr = (u8 *)(8ULL * i),
                    .operand_num = 0,
                    .usage_section = RelocSection::InitArray,
                    .addent = 0,
                });
            i++;
          }
          u32 size = 8 * i;
          init_array_sec->set_size(size);
          void *buff_data = malloc(size);
          memset(buff_data, 0, size);
          data_sec->set_data((const char *)buff_data, size);
          // IDK if i assume it copies it ??
          free(buff_data);
        }
        continue;
      }
      if (strcmp(global.name, "llvm.global_dtors") == 0) {
        // https://llvm.org/docs/Passes.html#lower-global-dtors-lower-global-destructors
        TODO("impl");
        continue;
      }
      label_usage_map.label_map[global.name].def_loc =
          curr_data_ptr - start_data;
      label_usage_map.label_map[global.name].kind = RelocKind::Data;
      label_usage_map.label_map[global.name].section =
          global.data.empty() ? RelocSection::Extern : RelocSection::Data;
      label_usage_map.label_map[global.name].size = global.size;
      label_usage_map.label_map[global.name].vis = global.vis;

      if (!global.data.empty()) {
        memcpy(curr_data_ptr, global.data.data(), global.data.size());
      }
      {  // handle reloccs
        for (const auto &reloc_info : global.reloc_info) {
          // fmt::println("RELOC INFO {}", reloc_info.name);
          ASSERT(label_usage_map.label_map.contains(reloc_info.name));
          label_usage_map.label_map[reloc_info.name].usage_loc.push_back(
              LabelRelocData::Usage{
                  .usage_instr = curr_data_ptr + reloc_info.insert_offset,
                  .operand_num = 0,
                  .usage_section = RelocSection::Data,
                  .addent = reloc_info.reloc_offset,
              });
        }
      }

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
    // fmt::println("{}", label_name.c_str());
    ASSERT(label_data.section != RelocSection::INVALID);

    Elf_Half sec_indx = 0;
    Elf_Word symbol_type = STT_FUNC;
    Elf_Word symbol_binding = STB_GLOBAL;

    switch (label_data.section) {
      case RelocSection::INVALID:
        UNREACH();
      case RelocSection::InitArray:
        sec_indx = init_array_sec->get_index();
        break;
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
    switch (label_data.binding) {
      case RelocBinding::INVALID:
        UNREACH();
      case RelocBinding::Global:
        symbol_binding = STB_GLOBAL;
        break;
      case RelocBinding::Weak:
        symbol_binding = STB_WEAK;
        break;
    }

    unsigned char other = 0;
    switch (label_data.vis) {
      case fir::LinkVisibility::Default:
        other |= STV_DEFAULT;
        break;
      case fir::LinkVisibility::Hidden:
        other |= STV_HIDDEN;
        break;
      case fir::LinkVisibility::Protected:
        other |= STV_PROTECTED;
        break;
    }

    auto symbol = syma.add_symbol(stra, label_name.c_str(), label_data.def_loc,
                                  label_data.size, symbol_binding, symbol_type,
                                  other, sec_indx);

    for (auto loc : label_data.usage_loc) {
      ASSERT(loc.usage_section != RelocSection::INVALID);
      switch (loc.usage_section) {
        case RelocSection::INVALID:
        case RelocSection::Extern:
          UNREACH();
        case RelocSection::InitArray:
          init_array_rela.add_entry((Elf64_Addr)loc.usage_instr, symbol,
                                    (unsigned char)R_X86_64_64, loc.addent);
          break;
        case RelocSection::Data:
          data_rela.add_entry(loc.usage_instr - start_data, symbol,
                              (unsigned char)R_X86_64_64, loc.addent);
          break;
        case RelocSection::Text:
          auto data = get_op_addr(loc.usage_instr, loc.operand_num);
          switch (label_data.section) {
            case RelocSection::INVALID:
            case RelocSection::InitArray:
              UNREACH();
            case RelocSection::Data:
              text_rela.add_entry(data.op_addr - start_txt, symbol,
                                  (unsigned char)R_X86_64_PC32,
                                  -data.op_off + data.op_val + loc.addent);
              break;
            case RelocSection::Extern:
            case RelocSection::Text:
              text_rela.add_entry(data.op_addr - start_txt, symbol,
                                  (unsigned char)R_X86_64_PLT32,
                                  -data.op_off + data.op_val + loc.addent);
              break;
          }
          break;
      }
    }
  }

  section *note_gnu_stack_sec = writer.sections.add(".note.GNU-stack");
  {  // section .note.GNU-stack noalloc noexec nowrite progbits
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
}  // namespace

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

  fmt::println(" Needs {} Relocations\n Generated {} Bytes\n",
               label_usages.label_map.size(), end_buff_ptr - output_buffer);

  generate_obj_file(label_usages, output_buffer, end_buff_ptr, decls, globals);

  // {
  //   ZyanU64 runtime_address = 0;
  //   ZyanUSize offset = 0;
  //   ZydisDisassembledInstruction instruction;
  //   while (ZYAN_SUCCESS(ZydisDisassembleIntel(
  //       /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
  //       /* runtime_address: */ runtime_address,
  //       /* buffer:          */ output_buffer + offset,
  //       /* length:          */ (end_buff_ptr - output_buffer) - offset,
  //       /* instruction:     */ &instruction))) {
  //     fmt::println("{:0>4x}: {}", runtime_address, instruction.text);
  //     offset += instruction.info.length;
  //     runtime_address += instruction.info.length;
  //   }
  // }
}

}  // namespace foptim::codegen
