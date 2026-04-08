#pragma once
#include "../module_pass.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/helper/inline.hpp"
#include "utils/job_system.hpp"

namespace foptim::optim {

class AlwaysInlineAdvisor {
  static constexpr bool debug_print = false;

 public:
  [[nodiscard]] bool should_be_inlined(const fir::Instr instr, CFG& /*cfg*/,
                                       Dominators& /*dom*/) {
    auto called_func = instr->get_arg(0);
    auto self_func = instr->get_parent()->get_parent();
    if (!called_func.is_constant() || !called_func.as_constant()->is_func()) {
      return false;
    }
    auto v = called_func.as_constant()->as_func();
    if (v->is_decl() || v->variadic) {
      ASSERT(!v->must_inline);
      return false;
    }
    if (debug_print) {
      fmt::println("Maybe inlining {} <- {}", self_func.func->name, v->name);
    }
    switch (v->linkage) {
      case fir::Linkage::Weak:
      case fir::Linkage::LinkOnce:
        if (debug_print) {
          fmt::println("N Bad linkage");
        }
        ASSERT(!v->must_inline);
        return false;
      case fir::Linkage::Internal:
      case fir::Linkage::External:
      case fir::Linkage::WeakODR:
      case fir::Linkage::LinkOnceODR:
        break;
    }

    // TODO: impl to hndle this correctly
    for (auto instr : v->basic_blocks[0]->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr) &&
          !instr->args[0].is_constant()) {
        if (debug_print) {
          fmt::println("{:cd}", instr);
          fmt::println("N dyn Alloca");
        }
        ASSERT(!v->must_inline);
        return false;
      }
    }
    if (debug_print) {
      fmt::println("Y");
    }
    return true;
  }
};

class BaseInlineAdvisor {
  u32 n_inlined_instructions = 0;
  u32 n_inlined_calls = 0;

  static constexpr bool debug_print = false;

 public:
  [[nodiscard]] bool should_be_inlined(const fir::Instr instr, CFG& cfg,
                                       Dominators& dom) {
    if (_should_be_inlined(instr, cfg, dom)) {
      auto v = instr->get_arg(0).as_constant()->as_func();
      n_inlined_calls += 1;
      n_inlined_instructions += v->n_instrs();
      return true;
    }
    return false;
  }

  [[nodiscard]] bool _should_be_inlined(const fir::Instr instr, CFG& cfg,
                                        Dominators& dom) const {
    auto called_func = instr->get_arg(0);
    auto self_func = instr->get_parent()->get_parent();
    auto self_n_instrs = self_func->n_instrs();
    if (!called_func.is_constant() || !called_func.as_constant()->is_func()) {
      return false;
    }
    auto v = called_func.as_constant()->as_func();
    const auto called_n_instrs = v->n_instrs();
    if (v->is_decl() || v->variadic) {
      if (v->must_inline) {
        fmt::println("Had must_inline but is variadic or only decl {:cd}",
                     instr);
        TODO("okak?");
      }
      return false;
    }
    if (debug_print) {
      fmt::println("Maybe inlining {} <- {}", self_func.func->name, v->name);
    }

    if (v->no_inline) {
      if (debug_print) {
        fmt::println("N No inline");
      }
      return false;
    }

    switch (v->linkage) {
      case fir::Linkage::Weak:
      case fir::Linkage::LinkOnce:
        if (debug_print) {
          fmt::println("N Bad linkage");
        }
        ASSERT(!v->must_inline);
        return false;
      case fir::Linkage::Internal:
      case fir::Linkage::External:
      case fir::Linkage::WeakODR:
      case fir::Linkage::LinkOnceODR:
        break;
    }

    // TODO: impl to hndle this correctly
    for (auto instr : v->basic_blocks[0]->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr) &&
          !instr->args[0].is_constant()) {
        if (debug_print) {
          fmt::println("{:cd}", instr);
          fmt::println("N dyn Alloca");
        }
        ASSERT(!v->must_inline);
        return false;
      }
    }

    if (v->must_inline) {
      if (debug_print) {
        fmt::println("Y Must inline");
      }
      return true;
    }

    if (called_n_instrs <=
        (4 + v.func->get_entry()->n_args() + (instr->has_result() ? 4 : 0))) {
      if (debug_print) {
        fmt::println("Y Shorter then setting up args");
      }
      return true;
    }

    bool all_args_are_constant = true;
    bool all_args_are_constant_or_allocas = true;
    for (auto arg : instr->args) {
      if (!arg.is_constant()) {
        all_args_are_constant = false;
        // TODO: could check for indexed allocas instructions
        if (!arg.is_instr() ||
            !arg.as_instr()->is(fir::InstrType::AllocaInstr)) {
          all_args_are_constant_or_allocas = false;
        }
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
    if (v->name.starts_with("_ZSt3min") || v->name.starts_with("_ZSt3max") ||
        v->name.starts_with("_ZSt3absf")) {
      if (debug_print) {
        fmt::println("Y Special");
      }
      return true;
    }

    if (n_inlined_instructions > 100 && n_inlined_calls > 50) {
      if (debug_print) {
        fmt::println("N already inlined a bunch");
      }
      return false;
    }

    if (self_n_instrs > 1000) {
      if (debug_print) {
        fmt::println("N self already big");
      }
      return false;
    }

    // if (v.func->get_n_uses() == 1 &&
    //     (v.func->linkage == fir::Linkage::Internal ||
    //      v.func->linkage == fir::Linkage::LinkOnceODR)) {
    //   if (debug_print) {
    //     fmt::println("Y single use");
    //   }
    //   return true;
    // }

    bool is_in_straightline_section = true;
    bool is_always_executed = true;
    {
      auto par_bb = instr->get_parent();
      auto par_func = par_bb->get_parent();
      auto par_bb_id = par_func->bb_id(par_bb);
      for (auto pred : cfg.bbrs[par_bb_id].pred) {
        if (dom.dominates(pred, par_bb_id)) {
          is_in_straightline_section = false;
          break;
        }
      }
      if (par_bb_id != cfg.entry) {
        auto sub_bb_id = 0;
        for (auto bb : par_func->basic_blocks) {
          if (bb->get_terminator()->is(fir::InstrType::ReturnInstr) &&
              !dom.dominates(sub_bb_id, par_bb_id)) {
            is_always_executed = false;
            break;
          }
          sub_bb_id++;
        }
      }
      // TODO need to check if dominates all exits
    }

    // fmt::println("{}, {}, {}, {}, {}", is_in_straightline_section,
    //              is_always_executed, v.func->n_bbs(), self_n_instrs,
    //              called_n_instrs);
    if (is_always_executed && v.func->n_bbs() == 1) {
      if (debug_print) {
        fmt::println("Y straight so always on hot parth");
      }
      return true;
    }

    if (is_in_straightline_section &&
        ((v.func->n_bbs() == 1 && called_n_instrs < 30))) {
      if (debug_print) {
        fmt::println("Y straight and short");
      }
      return true;
    }

    if (all_args_are_constant_or_allocas && self_n_instrs < 50 &&
        called_n_instrs < 10) {
      if (debug_print) {
        fmt::println(
            "Y all args are constnat + allocs might allow for mem2reg "
            "and shit");
      }
      return true;
    }

    if ((v.func->n_bbs() == 1 && called_n_instrs < 20) || called_n_instrs < 5 ||
        (self_func->name == "main" && called_n_instrs < 10)) {
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
concept InlineAdvisor =
    requires(T v, fir::Instr instr, CFG& cfg, Dominators& dom) {
      { v.should_be_inlined(instr, cfg, dom) } -> std::same_as<bool>;
    };

template <InlineAdvisor Advisor = BaseInlineAdvisor>
class Inline final : public ModulePass {
 public:
  void apply(fir::Context& ctx, JobSheduler* /*unused*/) override {
    ZoneScopedNC("INLINE", COLOR_OPTIMM);
    for (auto& f : ctx.data->storage.functions) {
      apply(ctx, *f.second);
    }
  }

  void apply(fir::Context& /*unused*/, fir::Function& func) {
    Advisor adv;
    TVec<fir::Instr> calls;

    for (u8 trys = 0; trys < 4; trys++) {
      calls.clear();
      if (func.is_decl()) {
        continue;
      }
      CFG cfg{func};
      Dominators dom{cfg};

      for (size_t bb_id = 0; bb_id < func.n_bbs(); bb_id++) {
        auto bb = func.basic_blocks[bb_id];
        for (size_t instr_id = 0; instr_id < bb->n_instrs(); instr_id++) {
          if (bb->instructions[instr_id]->is(fir::InstrType::CallInstr) &&
              adv.should_be_inlined(bb->instructions[instr_id], cfg, dom)) {
            calls.push_back(bb->instructions[instr_id]);
            break;
          }
        }
      }
      if (calls.empty()) {
        break;
      }
      for (auto call : calls) {
        inline_call(call);
        // if(!success){
        //   fmt::println("{}", call);
        //   fmt::println("{}", *call->args[0].as_constant()->as_func().func);
        // }
        // ASSERT(success);
      }
    }
  }
};

}  // namespace foptim::optim
