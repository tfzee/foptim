#pragma once
#include "../module_pass.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/helper/inline.hpp"

namespace foptim::optim {

class BaseInlineAdvisor {
  u32 n_inlined_instructions = 0;
  u32 n_inlined_calls = 0;

public:
  bool should_be_inlined(const fir::Instr instr) {
    if (_should_be_inlined(instr)) {
      auto v = instr->get_arg(0).as_constant()->as_func();
      n_inlined_calls += 1;
      n_inlined_instructions += v->n_instrs();
      return true;
    }
    return false;
  }

  bool _should_be_inlined(const fir::Instr instr) {
    // auto func = instr->get_parent()->get_parent();
    auto called_func = instr->get_arg(0);
    if (!called_func.is_constant() || !called_func.as_constant()->is_func()) {
      return false;
    }
    auto v = called_func.as_constant()->as_func();
    if (v->is_decl() || v->variadic) {
      return false;
    }
    // TODO: impl to hndle this correctly
    for (auto instr : v->basic_blocks[0]->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr)) {
        return false;
      }
    }

    switch (v->linkage) {
    case fir::Function::Linkage::Weak:
    case fir::Function::Linkage::LinkOnce:
      return false;
    case fir::Function::Linkage::Internal:
    case fir::Function::Linkage::External:
    case fir::Function::Linkage::WeakODR:
    case fir::Function::Linkage::LinkOnceODR:
      break;
    }

    if (v->n_instrs() <= (5 + v.func->get_entry()->n_args())) {
      return true;
    }

    bool all_args_are_constant = true;
    for (auto arg : instr->args) {
      if (!arg.is_constant()) {
        all_args_are_constant = false;
      }
    }
    if (all_args_are_constant) {
      return true;
    }
    // NOTE: this aint perfect it would also try to inlnie namespace std {
    // namespace min { void someFunc(); }}
    if (v->name.starts_with("_ZSt3min") || v->name.starts_with("_ZSt3max")) {
      return true;
    }

    if (n_inlined_instructions < 100 && n_inlined_calls < 50) {
      return false;
    }

    if (v.func->get_n_uses() == 1 &&
        (v.func->linkage == fir::Function::Function::Linkage::Internal ||
         v.func->linkage == fir::Function::Function::Linkage::LinkOnceODR)) {
      return true;
    }

    if (v.func->n_instrs() <=
        (2 + v.func->get_entry()->n_args() + (instr->has_result() ? 1 : 0))) {
      return true;
    }

    bool is_in_straightline_section = true;
    {
      auto par_bb = instr->get_parent();
      auto par_func = par_bb->get_parent();
      CFG cfg{*par_func.func};
      Dominators dom{cfg};
      auto par_bb_id = par_func->bb_id(par_bb);
      if (dom.dom_bbs[par_bb_id].dominators[par_bb_id]) {
        is_in_straightline_section = false;
      }
      for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
        if (!cfg.bbrs[bb_id].succ.empty()) {
          continue;
        }
        if (!dom.dom_bbs[bb_id].dominators[par_bb_id]) {
          is_in_straightline_section = false;
          break;
        }
      }
    }

    if (is_in_straightline_section &&
        ((v.func->n_bbs() == 1 && v.func->n_instrs() < 50))) {
      return true;
    }
    if (v.func->n_instrs() < 5) {
      return true;
    }
    return false;
  }
};

template <typename T>
concept InlineAdvisor = requires(T v, fir::Instr instr) {
  { v.should_be_inlined(instr) } -> std::same_as<bool>;
};

template <InlineAdvisor Advisor = BaseInlineAdvisor>
class Inline final : public ModulePass {
public:
  void apply(fir::Context &ctx) override {
    ZoneScopedN("INLINE");
    for (auto &f : ctx.data->storage.functions) {
      apply(ctx, f.second);
    }
  }

  void apply(fir::Context & /*unused*/, fir::Function &func) {
    Advisor adv;
    TVec<fir::Instr> calls;

    for (u8 trys = 0; trys < 4; trys++) {
      calls.clear();
      for (size_t bb_id = 0; bb_id < func.n_bbs(); bb_id++) {
        auto bb = func.basic_blocks[bb_id];
        for (size_t instr_id = 0; instr_id < bb->n_instrs(); instr_id++) {
          if (bb->instructions[instr_id]->is(fir::InstrType::CallInstr) &&
              adv.should_be_inlined(bb->instructions[instr_id])) {
            calls.push_back(bb->instructions[instr_id]);
            break;
          }
        }
      }
      if (calls.empty()) {
        break;
      }
      for (auto call : calls) {
        auto success = inline_call(call);
        ASSERT(success);
      }
    }
  }
};

} // namespace foptim::optim
