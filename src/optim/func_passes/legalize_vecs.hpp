#pragma once
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "optim/function_pass.hpp"
#include "utils/string.hpp"
#include <fmt/core.h>

namespace foptim::optim {

class LegalizeVecs final : public FunctionPass {

  void legalize(fir::Context &ctx, fir::Instr instr) {
    for (size_t arg_id = 0; arg_id < instr->args.size(); arg_id++) {
      auto arg = instr->args[arg_id];
      // replace vec with reference to global variable
      if (arg.is_constant() && arg.as_constant()->is_vec()) {
        // TODO: prob should check for duplicates here
        auto constant = arg.as_constant();
        IRString name;
        fmt::format_to(std::back_inserter(name), "global_vec_const_{}",
                       (void *)constant.get_raw_ptr());
        const auto &typ = constant->type->as_vec();
        const auto actual_size = typ.get_size();
        // TODO assert that this doesnt actually already exists
        auto global = ctx->get_global(name, actual_size);
        global->is_constant = true;
        global->linkage = fir::Linkage::Internal;
        global->init_value =
            foptim::utils::IRAlloc<uint8_t>{}.allocate(actual_size);
        memset(global->init_value, 0, actual_size);

        convert_constant_init(global->init_value, constant, global);

        fir::Builder bb{instr};
        auto load_val = bb.build_load(
            constant->type, fir::ValueR{ctx->get_constant_value(global)});
        instr.replace_arg(arg_id, load_val);
      }
    }
  }

public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    for (auto bb : func.basic_blocks) {
      for (size_t instr_id = 0; instr_id < bb->instructions.size();
           instr_id++) {
        legalize(ctx, bb->instructions[instr_id]);
      }
    }
  }
};
} // namespace foptim::optim
