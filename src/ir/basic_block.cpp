#include "basic_block.hpp"
#include "function.hpp"
#include "instruction_data.hpp"
#include "ir/basic_block_ref.hpp"

namespace foptim::fir {

// Also handles a bunch of cleanup stuff
void BasicBlockData::remove_instr(size_t indx, bool delete_instr) {
  if (delete_instr) {
    instructions[indx]->set_parent(BasicBlock(BasicBlock::invalid()));
    instructions[indx]->remove_all_usages();
    instructions[indx].clear_args();
    instructions[indx].clear_bbs();
    instructions[indx]._invalidate();
  } else {
    instructions[indx]->set_parent(BasicBlock(BasicBlock::invalid()));
    instructions[indx]->remove_all_usages();
    instructions[indx].clear_args();
    instructions[indx].clear_bbs();
  }
  instructions.erase(instructions.begin() + (i64)indx);
}

bool BasicBlockData::verify(const Function *exp_parent) const {
  if ((func.func == nullptr) || func.func != exp_parent) {
    fmt::println(" BasicBlocks parent does not match the function it is in");
    return false;
  }
  // TODO args

  for (const auto &instr : instructions) {
    if (!instr.is_valid() || !instr->verify(this)) {
      fmt::println("Invalid Instruction {:p}", (void *)instr.get_raw_ptr());
      return false;
    }
  }
  return true;
}

void BasicBlockData::remove_arg(size_t i) {
  args[i]->replace_all_uses(ValueR());
  args[i]->_parent = fir::BasicBlock(fir::BasicBlock::invalid());
  args.erase(args.begin() + i);
}

void BasicBlockData::clear_args() {
  for (auto &arg : args) {
    arg->replace_all_uses(ValueR());
  }
  args.clear();
}

void BasicBlockData::remove_from_parent(bool remove_references,
                                        bool cleanup_instr, bool delete_instr) {
  if (remove_references) {
    // replace_all_uses(ValueR());
    for (auto &arg : args) {
      arg->replace_all_uses(ValueR());
    }
  }
  if (cleanup_instr) {
    while (!instructions.empty()) {
      remove_instr(instructions.size() - 1, delete_instr);
    }
  }
  for (size_t t = 0; t < func->basic_blocks.size(); t++) {
    if (func->basic_blocks[t].operator->() == this) {
      func->basic_blocks.erase(func->basic_blocks.begin() + t);
      return;
    }
  }
  TODO("unreach?");
}

// FVec<Instr> &BasicBlockData::get_instrs() { return instructions; }
} // namespace foptim::fir
