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

  static constexpr bool debug_print = true;

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
    auto called_func = instr->get_arg(0);
    if (!called_func.is_constant() || !called_func.as_constant()->is_func()) {
      return false;
    }
    auto v = called_func.as_constant()->as_func();
    if (v->is_decl() || v->variadic) {
      return false;
    }
    if (debug_print) {
      fmt::println("Maybe inlining {}", v->name);
    }

    if (v->no_inline) {
      if (debug_print) {
        fmt::println("N No inline");
      }
      return false;
    }
    if (v->must_inline) {
      if (debug_print) {
        fmt::println("Y Must inline");
      }
      return true;
    }

    switch (v->linkage) {
    case fir::Linkage::Weak:
    case fir::Linkage::LinkOnce:
      if (debug_print) {
        fmt::println("N Bad linkage");
      }
      return false;
    case fir::Linkage::Internal:
    case fir::Linkage::External:
    case fir::Linkage::WeakODR:
    case fir::Linkage::LinkOnceODR:
      break;
    }

    // TODO: impl to hndle this correctly
    for (auto instr : v->basic_blocks[0]->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr)) {
        if (debug_print) {
          fmt::println("N Alloca");
        }
        return false;
      }
    }

    if (v->n_instrs() <= (5 + v.func->get_entry()->n_args())) {
      if (debug_print) {
        fmt::println("Y Shorter then setting up args");
      }
      return true;
    }

    bool all_args_are_constant = true;
    for (auto arg : instr->args) {
      if (!arg.is_constant()) {
        all_args_are_constant = false;
      }
    }
    if (all_args_are_constant) {
      if (debug_print) {
        fmt::println("Y All args const");
      }
      return true;
    }
    // NOTE: this aint perfect it would also try to inlnie namespace std {
    // namespace min { void someFunc(); }}
    if (v->name.starts_with("_ZSt3min") || v->name.starts_with("_ZSt3max")) {
      if (debug_print) {
        fmt::println("Y Special");
      }
      return true;
    }

    if (n_inlined_instructions < 100 && n_inlined_calls < 50) {
      if (debug_print) {
        fmt::println("N already inlined a bunch");
      }
      return false;
    }

    if (v.func->get_n_uses() == 1 &&
        (v.func->linkage == fir::Linkage::Internal ||
         v.func->linkage == fir::Linkage::LinkOnceODR)) {
      if (debug_print) {
        fmt::println("Y single use");
      }
      return true;
    }

    if (v.func->n_instrs() <=
        (2 + v.func->get_entry()->n_args() + (instr->has_result() ? 1 : 0))) {
      if (debug_print) {
        fmt::println("Y shorter the nsetup");
      }
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
      if (debug_print) {
        fmt::println("Y straight  and short");
      }
      return true;
    }
    if (v.func->n_instrs() < 5) {
      if (debug_print) {
        fmt::println("Y short");
      }
      return true;
    }
    if (debug_print) {
      fmt::println("N end");
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
      apply(ctx, *f.second);
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
