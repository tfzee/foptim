#include "helper.hpp"

#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

void flip_cond_branch(fir::Instr cond_term) {
  ASSERT(cond_term->is(fir::InstrType::CondBranchInstr));

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

GuessTypeResult guessType(fir::ValueR ptr) {
  if (ptr.is_constant()) {
    return {.typeless = true, .type = fir::TypeR{fir::TypeR::invalid()}};
  }
  if (ptr.is_bb_arg()) {
    // auto bb_arg = ptr.as_bb_arg();
    // if (bb_arg->_parent == bb_arg->_parent->get_parent()->get_entry()) {
    return {.typeless = true, .type = fir::TypeR{fir::TypeR::invalid()}};
    // }
  }
  if (ptr.is_instr()) {
    auto ptr_instr = ptr.as_instr();
    if (ptr_instr->is(fir::InstrType::AllocaInstr)) {
      if (ptr_instr->has_attrib("alloca::type")) {
        return {.typeless = false,
                .type = *ptr_instr->get_attrib("alloca::type").try_type()};
      }
      return {.typeless = true, .type = fir::TypeR{fir::TypeR::invalid()}};
    }
    if (ptr_instr->is(fir::InstrType::LoadInstr)) {
      return {.typeless = true, .type = fir::TypeR{fir::TypeR::invalid()}};
    }
    if (ptr_instr->is(fir::InstrType::BinaryInstr) &&
        (fir::BinaryInstrSubType)ptr_instr->subtype ==
            fir::BinaryInstrSubType::IntAdd) {
      GuessTypeResult out_res = guessType(ptr_instr->args[0]);
      GuessTypeResult r2 = guessType(ptr_instr->args[1]);
      if (out_res.typeless && !r2.typeless) {
        return r2;
      }
      if (!out_res.typeless && !r2.typeless) {
        return {.typeless = false, .type = fir::TypeR{fir::TypeR::invalid()}};
      }
      return out_res;
    }
  }
  return {.typeless = false, .type = fir::TypeR{fir::TypeR::invalid()}};
}

}  // namespace foptim::optim
