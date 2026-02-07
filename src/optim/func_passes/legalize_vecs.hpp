#pragma once
#include <fmt/core.h>

#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction_data.hpp"
#include "optim/function_pass.hpp"
#include "utils/string.hpp"

namespace foptim::optim {

class LegalizeVecs final : public FunctionPass {
  bool legalize(fir::Context &ctx, fir::Instr instr) {
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
        auto global = ctx->insert_global(name, actual_size);
        global->is_constant = true;
        global->linkage = fir::Linkage::Internal;
        global->init_value =
            foptim::utils::IRAlloc<uint8_t>{}.allocate(actual_size);
        memset(global->init_value, 0, actual_size);

        convert_constant_init(global->init_value, constant, global);

        fir::Builder bb{instr};
        auto load_val = bb.build_load(
            constant->type, fir::ValueR{ctx->get_constant_value(global)}, false, false);
        instr.replace_arg(arg_id, load_val);
      }
    }
    if (instr->is(fir::InstrType::Intrinsic) &&
        instr->subtype == (u32)fir::IntrinsicSubType::FAbs &&
        instr->get_type()->is_vec()) {
      fir::Builder b{instr};
      auto width = instr->get_type()->as_vec().bitwidth;
      auto f_type = ctx->get_float_type(width);
      ASSERT(width == 64 || width == 32);
      fir::ValueR broad{};
      if (width == 64) {
        broad = b.build_vbroadcast(
            fir::ValueR{ctx->get_constant_value(
                std::bit_cast<f64>((u64)0x7fffffffffffffff), f_type)},
            instr->get_type());
      } else {
        broad = b.build_vbroadcast(
            fir::ValueR{ctx->get_constant_value(
                std::bit_cast<f32>((u32)0x7fffffff), f_type)},
            instr->get_type());
      }
      auto r = b.build_binary_op(instr->args[0], broad,
                                 fir::BinaryInstrSubType::And);
      instr->replace_all_uses(r);
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
