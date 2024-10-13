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
  FVec<BasicBlock> basic_blocks;
  BasicBlock entry;
  // metadata
  CallingConv cc = CallingConv::C;
  Linkage linkage = Linkage::Internal;

  Function(ContextData *ctx, std::string name, FunctionTypeR type)
      : ctx(ctx), name(std::move(name)), func_ty(type), basic_blocks({}), entry({}) {}

  std::string &getName() { return name; }
  const std::string &getName() const { return name; }

  FVec<BasicBlock> &get_bbs() { return basic_blocks; }
  const FVec<BasicBlock> &get_bbs() const { return basic_blocks; }

  size_t n_instrs() const {
    size_t n_instrs = 0;
    for (const auto bb : basic_blocks) {
      n_instrs += bb->n_instrs();
    }
    return n_instrs;
  }
  size_t n_bbs() const { return basic_blocks.size(); }

  void append_bbr(BasicBlock bb) { basic_blocks.push_back(bb); }
  void set_entry_bbr(BasicBlock bb) {
    // TODO: verify that its basic block in the local list;

    ASSERT_M(bb->args.size() == 0,
             "Basic block args need to empty to be set as entry block");

    const auto &arg_tys = func_ty->as_func_ty().arg_types;
    bb->args.reserve(arg_tys.size());
    for (auto arg_ty : arg_tys) {
      bb->args.emplace_back(arg_ty);
    }
    entry = bb;
  }

  bool verify(utils::Printer) const;
  constexpr BasicBlock get_entry_bb() const { return entry; }
};

} // namespace foptim::fir
