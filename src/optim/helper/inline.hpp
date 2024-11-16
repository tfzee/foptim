#pragma once
#include "ir/basic_block_ref.hpp"
#include <Tracy/tracy/Tracy.hpp>

namespace foptim::optim {

//Splits after the instruction leaving the instr in the original block
//@returns the new basic block
fir::BasicBlock split_block(fir::Instr at_instr);

//inlines a function call
//@returns true if successfull
bool inline_call(fir::Instr call);

}
