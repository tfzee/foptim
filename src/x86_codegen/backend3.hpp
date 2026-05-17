#pragma once
#include "ir/helpers.hpp"
#include "mir/global.hpp"
#include "utils/map.hpp"
#include "utils/string.hpp"
#include "utils/vec.hpp"
#include <span>
namespace foptim::codegen {

enum class ProEpilogueType {
  Full = 0,  // full epilogue for setting up bp and sp
  Align = 1, // just aligning the stack for calls not setting up sp/bp
  None = 2,  // doing nothing
};

enum class RelocSection : u8 {
  INVALID,
  Data,
  Text,
  InitArray,
  Extern,
};
enum class RelocKind : u8 {
  INVALID,
  BB,
  Func,
  Data,
};
enum class RelocBinding : u8 {
  INVALID,
  Global,
  Weak,
};

struct LabelRelocData {
  struct Usage {
    // ptr to instruction
    u8 *usage_instr;
    u8 operand_num;
    RelocSection usage_section = RelocSection::INVALID;
    size_t addent;
  };

  // absolute location *inside* of the section
  RelocKind kind = RelocKind::INVALID;
  RelocSection section = RelocSection::INVALID;
  RelocBinding binding = RelocBinding::Global;
  fir::LinkVisibility vis = fir::LinkVisibility::Default;
  u64 def_loc = 0;
  u64 size = 0;
  TVec<Usage> usage_loc;
};

struct TLabelUsageMap {
  TMap<IRString, LabelRelocData> label_map;
  TMap<u32, LabelRelocData> bb_map;

  void insert_label_ref(const IRString &label, u8 *instr_loc, u8 op_num,
                        RelocSection section) {
    label_map[label].usage_loc.push_back({instr_loc, op_num, section, 0});
  }
  void insert_bb_ref(const u32 bb, u8 *instr_loc, u8 op_num,
                     RelocSection section) {
    bb_map[bb].usage_loc.push_back({instr_loc, op_num, section, 0});
  }
};

void run(std::span<const fmir::MFunc> funcs, std::span<const IRString> decls,
         std::span<const fmir::Global> globals, const conf::CompConf &conf);

} // namespace foptim::codegen
