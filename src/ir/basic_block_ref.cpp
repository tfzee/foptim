#include "basic_block_ref.hpp"
#include "ir/basic_block.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::fir {

void BasicBlock::insert_instr(size_t indx, Instr instr) {
  auto self = operator->();
  instr->set_parent(*this);
  self->instructions.insert(self->instructions.begin() + indx, instr);
}
} // namespace foptim::fir
