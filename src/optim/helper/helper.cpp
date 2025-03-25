#include "helper.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

void flip_cond_branch(fir::Instr cond_term) {
  ASSERT(cond_term->is(fir::InstrType::CondBranchInstr));
  fmt::println("COND TERM {}", cond_term);

  auto builder = cond_term->parent.builder();
  builder.at_penultimate(cond_term->parent);

  auto negated_value =
      builder.build_unary_op(cond_term->args[0], fir::UnaryInstrSubType::Not);

  cond_term.replace_arg(0, negated_value);

  auto arg0 = cond_term->bbs[0];
  auto arg1 = cond_term->bbs[1];

  cond_term.replace_bb(0, arg1.bb, false, false);
  for (auto arg : arg1.args) {
    cond_term.add_bb_arg(0, arg);
  }
  cond_term.replace_bb(1, arg0.bb, false, false);
  for (auto arg : arg0.args) {
    cond_term.add_bb_arg(1, arg);
  }
}

} // namespace foptim::optim
