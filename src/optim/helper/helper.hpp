#pragma once
#include "ir/instruction.hpp"

namespace foptim::optim {
void flip_cond_branch(fir::Instr cond_term);
}
