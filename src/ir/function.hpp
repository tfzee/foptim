#pragma once
#include "basic_block.hpp"
#include "basic_block_ref.hpp"
#include "ir/attributable.hpp"
#include "ir/helpers.hpp"
#include "types_ref.hpp"
#include "utils/string.hpp"
#include "utils/todo.hpp"
#include <utility>

namespace foptim::fir {

class Function : public Attributable, public LockedUsed {
public:
  enum class CallingConv { C, Dynamic };

  ContextData *ctx;
  IRString name;
  FunctionTypeR func_ty;
  IRVec<BasicBlock> basic_blocks;
  // metadata
  CallingConv cc = CallingConv::C;
  Linkage linkage = Linkage::Internal;
  LinkVisibility linkvis = LinkVisibility::Default;
  u8 variadic : 1 = 0;
  u8 must_progress : 1 = 0;
  u8 no_recurse : 1 = 0;
  u8 no_inline : 1 = 0;
  u8 must_inline : 1 = 0;
  u8 mem_write_only : 1 = 0;
  u8 mem_read_only : 1 = 0;
  u8 mem_read_none : 1 = 0;

  Function(ContextData *ctx, IRString name, FunctionTypeR type)
      : ctx(ctx), name(std::move(name)), func_ty(type), basic_blocks({}) {}

  bool is_decl() const { return basic_blocks.empty(); }
  IRString &getName() { return name; }
  const IRString &getName() const { return name; }

  [[nodiscard]] BasicBlock get_entry() const {
    ASSERT(!basic_blocks.empty());
    return basic_blocks[0];
  }
  auto &get_bbs() { return basic_blocks; }
  const auto &get_bbs() const { return basic_blocks; }

  size_t n_instrs() const {
    size_t n_instrs = 0;
    for (const auto bb : basic_blocks) {
      n_instrs += bb->n_instrs();
    }
    return n_instrs;
  }
  size_t n_bbs() const { return basic_blocks.size(); }

  /*
    @returns index of the given bb or aborts on invalid bb
  */
  [[nodiscard]] size_t bb_id(BasicBlock b) const;
  void append_bbr(BasicBlock bb) {
    bb->func = FunctionR{this};
    basic_blocks.push_back(bb);
  }
  bool verify() const;
};

} // namespace foptim::fir
