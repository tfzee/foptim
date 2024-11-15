#pragma once
#include "basic_block.hpp"
#include "basic_block_ref.hpp"
#include "ir/attributable.hpp"
#include "types.hpp"
#include "types_ref.hpp"
#include "utils/todo.hpp"
#include <string>
#include <utility>

namespace foptim::utils {
class Printer;
}
namespace foptim::fir {
struct ContextData;

class Function : public Attributable {
public:
  enum class CallingConv { C, Dynamic };
  enum class Linkage {
    Internal,
    External,
  };

  ContextData *ctx;
  std::string name;
  FunctionTypeR func_ty;
  IRVec<BasicBlock> basic_blocks;
  // metadata
  CallingConv cc = CallingConv::C;
  Linkage linkage = Linkage::Internal;

  Function(ContextData *ctx, std::string name, FunctionTypeR type)
      : ctx(ctx), name(std::move(name)), func_ty(type), basic_blocks({}) {}

  std::string &getName() { return name; }
  const std::string &getName() const { return name; }

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
  void append_bbr(BasicBlock bb) { basic_blocks.push_back(bb); }
  bool verify(utils::Printer) const;
};

} // namespace foptim::fir
