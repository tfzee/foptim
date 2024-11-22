#include "basic_block_ref.hpp"
#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::fir {

Builder BasicBlock::builder() {
  return {*this};  
}

Builder BasicBlock::builder_at_end() {
  Builder b = {*this};
  b.at_end(*this);
  return b;  
}

void BasicBlock::insert_instr(size_t indx, Instr instr) {
  auto *self = operator->();
  instr->set_parent(*this);
  self->instructions.insert(self->instructions.begin() + indx, instr);
}

void BasicBlock::push_instr(Instr instr) {
  auto *self = operator->();
  instr->set_parent(*this);
  self->instructions.push_back(instr);
}

ValueR BasicBlock::add_arg(TypeR type) {
  auto *self = operator->();
  self->args.emplace_back(type);
  return ValueR{*this, (u32)self->n_args() - 1};
}
} // namespace foptim::fir
