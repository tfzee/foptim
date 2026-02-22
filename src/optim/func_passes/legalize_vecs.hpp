#pragma once
#include <fmt/core.h>

#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction_data.hpp"
#include "optim/function_pass.hpp"
#include "utils/parameters.hpp"
#include "utils/string.hpp"

namespace foptim::optim {

class LegalizeVecs final : public FunctionPass {
  static void push_all_uses(TVec<fir::Instr> &worklist, fir::Instr instr) {
    for (auto u : instr->get_uses()) {
      worklist.push_back(u.user);
    }
  }

  bool legalize(fir::Context &ctx, fir::Instr instr,
                TVec<fir::Instr> &worklist) {
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
            constant->type, fir::ValueR{ctx->get_constant_value(global)}, false,
            false);
        instr.replace_arg(arg_id, load_val);
        return true;
      }
    }

    if (instr->is(fir::InstrType::FCmp) && instr->args[1].is_constant_float()) {
      auto constant = instr->args[1].as_constant();
      IRString name;
      fmt::format_to(std::back_inserter(name), "global_float_const_{}",
                     (void *)constant.get_raw_ptr());
      const auto size = constant->type->as_float();
      // TODO assert that this doesnt actually already exists
      auto global = ctx->insert_global(name, size);
      global->is_constant = true;
      global->linkage = fir::Linkage::Internal;
      global->init_value = foptim::utils::IRAlloc<uint8_t>{}.allocate(size);
      memset(global->init_value, 0, size);

      convert_constant_init(global->init_value, constant, global);

      fir::Builder bb{instr};
      auto load_val = bb.build_load(
          constant->type, fir::ValueR{ctx->get_constant_value(global)}, false,
          false);
      instr.replace_arg(1, load_val);
      return true;
    }

    if (instr->is(fir::IntrinsicSubType::FAbs) && instr->get_type()->is_vec()) {
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
      push_all_uses(worklist, instr);
      instr->replace_all_uses(r);
      instr.destroy();
      return true;
    }
    // TODO: is this check for avx512 sufficient/correct?
    if (instr->is(fir::VectorISubType::Broadcast) && !utils::enable_avx512f &&
        instr->get_type()->get_bitwidth() >= 512) {
      // auto funcy = instr->get_parent()->get_parent().func;
      // fmt::println("{:cd}", *funcy);
      // fmt::println("{}", instr);
      fir::Builder buh{instr};
      auto old_type = instr->get_type()->as_vec();
      auto new_type = ctx->get_vec_type(old_type.type, old_type.bitwidth,
                                        old_type.member_number / 2);
      auto a1 = buh.build_vbroadcast(instr->args[0], new_type);
      auto a2 = buh.build_vbroadcast(instr->args[0], new_type);
      auto r = buh.build_vector_op(a1, a2, instr->get_type(),
                                   fir::VectorISubType::Concat);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(r);
      instr.destroy();
      // fmt::println("{:cd}", *funcy);
      // TODO("split");
      return true;
    }
    // TODO: is this check for avx512 sufficient/correct?
    if (instr->is(fir::VectorISubType::HorizontalAdd) &&
        !utils::enable_avx512f &&
        instr->args[0].get_type()->get_bitwidth() >= 512) {
      fir::Builder buh{instr};
      auto out_type = instr->get_type();
      auto old_type = instr->args[0].get_type()->as_vec();
      auto new_type = ctx->get_vec_type(old_type.type, old_type.bitwidth,
                                        old_type.member_number / 2);
      auto al = buh.build_vector_op(instr->args[0], new_type,
                                    fir::VectorISubType::ExtractLow);
      auto ah = buh.build_vector_op(instr->args[0], new_type,
                                    fir::VectorISubType::ExtractHigh);
      auto a1 =
          buh.build_vector_op(al, out_type, fir::VectorISubType::HorizontalAdd);
      auto a2 =
          buh.build_vector_op(ah, out_type, fir::VectorISubType::HorizontalAdd);
      ASSERT(out_type->is_float());  // TODO: impl for int
      auto r = buh.build_float_add(a1, a2);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(r);
      instr.destroy();
      return true;
    }
    return false;
  }

 public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    TVec<fir::Instr> worklist;
    // TODO(PERF): do prefiltering
    for (auto bb : func.basic_blocks) {
      for (size_t instr_id = 0; instr_id < bb->instructions.size();
           instr_id++) {
        worklist.push_back(bb->instructions[instr_id]);
      }
    }

    while (!worklist.empty()) {
      auto curr = worklist.back();
      worklist.pop_back();
      legalize(ctx, curr, worklist);
    }
  }
};
}  // namespace foptim::optim
