#pragma once
#include <fmt/core.h>

#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction_data.hpp"
#include "optim/function_pass.hpp"
#include "utils/string.hpp"

namespace foptim::optim {

class LegalizeStructs final : public FunctionPass {
  bool legalize(fir::Context &ctx, fir::Instr instr) {
    if (instr->get_type()->is_struct() &&
        instr->is(fir::InstrType::LoadInstr)) {
      auto ty = instr->get_type()->as_struct();
      fir::Builder b{instr};

      auto in_ptr = instr->args[0];
      auto v = fir::ValueR{ctx->get_poisson_value(instr->get_type())};

      size_t index = 0;
      for (auto member : ty.elems) {
        auto in_off = b.build_int_add(
            in_ptr, fir::ValueR{ctx->get_constant_int(member.offset, 64)});
        auto loaded = b.build_load(member.ty, in_off);
        fir::ValueR args[1] = {fir::ValueR{ctx->get_constant_int(index, 64)}};
        v = b.build_insert_value(v, loaded, args, instr->get_type());
        index++;
      }
      instr->replace_all_uses(v);
      instr.destroy();
      return true;
    }
    return false;
  }

 public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    for (auto bb : func.basic_blocks) {
      for (size_t instr_id = 0; instr_id < bb->instructions.size();
           instr_id++) {
        if (legalize(ctx, bb->instructions[instr_id])) {
          instr_id = 0;
        }
      }
    }
  }
};
}  // namespace foptim::optim
