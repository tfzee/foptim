#include "inline.hpp"
#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

using namespace fir;

fir::BasicBlock split_block(fir::Instr at_instr) {
  // auto *ctx = at_instr->get_parent()->get_parent()->ctx;
  auto t_bb = at_instr->get_parent();
  auto t_func = t_bb->get_parent();

  auto bb = t_func.builder();
  auto new_bb = bb.append_bb();

  size_t instr_id = 0;
  while(t_bb->instructions[instr_id] != at_instr){
    instr_id++;
    ASSERT(instr_id < t_bb->instructions.size());
  }
  instr_id++;

  auto start_id = instr_id;
  for(; instr_id < t_bb->instructions.size(); instr_id++){
    new_bb.push_instr(t_bb->instructions[instr_id]);
  }
  t_bb->instructions.erase(t_bb->instructions.begin() + start_id, t_bb->instructions.end());
  return new_bb;  
}

bool inline_call(fir::Instr call) {
  ASSERT(call->is(InstrType::DirectCallInstr));

  // auto* ctx = call->get_parent()->get_parent()->ctx;
  // auto call_bb = call->get_parent();
  // auto call_func = call_bb->get_parent();
  // auto callee = call->get_attrib("callee");

  // auto end_bb = split_block(call);


  TODO("impl");
  return false;
}

} // namespace foptim::optim
