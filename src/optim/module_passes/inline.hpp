#pragma once
#include <fmt/base.h>

#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/callgraph.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/helper/inline.hpp"
#include "optim/module_pass.hpp"
#include "utils/job_system.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {
struct InlineConfig {
  bool allow_recursive = true;
  bool allow_builtins = true;
};

// Looks through constant offset PtrAdds to find the alloca a pointer points
// into, e.g. a member of a stack object.
inline bool points_into_alloca(fir::ValueR v) {
  while (v.is_instr()) {
    auto i = v.as_instr();
    if (i->is(fir::InstrType::AllocaInstr)) {
      return true;
    }
    if (i->is(fir::BinaryInstrSubType::PtrAdd) &&
        i->args[1].is_constant_int()) {
      v = i->args[0];
      continue;
    }
    return false;
  }
  return false;
}

// Facts about a callee that do not depend on the call site. Computed once per
// pass run and reused by all its call sites.
struct InlineCallee {
  // declaration, variadic, no_inline, bad linkage or dynamic alloca
  bool never = false;
  u32 n_instrs = 0;
  u32 n_bbs = 0;
  u32 n_args = 0;
};

class InlineCalleeInfos {
  TMap<fir::Function *, InlineCallee> infos;

  static InlineCallee compute(fir::Function &v, bool honor_no_inline) {
    InlineCallee res;
    res.n_instrs = static_cast<u32>(v.n_instrs());
    res.n_bbs = static_cast<u32>(v.n_bbs());
    if (v.is_decl() || v.attribs.variadic) {
      if (v.attribs.must_inline) {
        fmt::print(fg(fmt::color::red),
                   "Had must_inline but is variadic or only decl {:cd}", v);
      }
      res.never = true;
      return res;
    }
    res.n_args = static_cast<u32>(v.get_entry()->n_args());
    if (honor_no_inline && v.attribs.no_inline) {
      res.never = true;
      return res;
    }
    switch (v.attribs.linkage) {
      case fir::Linkage::Weak:
      case fir::Linkage::LinkOnce:
        ASSERT(!v.attribs.must_inline);
        res.never = true;
        return res;
      case fir::Linkage::Internal:
      case fir::Linkage::External:
      case fir::Linkage::WeakODR:
      case fir::Linkage::LinkOnceODR:
        break;
    }
    // TODO: impl to hndle this correctly
    for (auto instr : v.basic_blocks[0]->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr) &&
          !instr->args[0].is_constant()) {
        ASSERT(!v.attribs.must_inline);
        res.never = true;
        return res;
      }
    }
    return res;
  }

 public:
  const InlineCallee &get(fir::Function &v, bool honor_no_inline) {
    auto it = infos.find(&v);
    if (it == infos.end()) {
      it = infos.emplace(&v, compute(v, honor_no_inline)).first;
    }
    return it->second;
  }
  // the body of f changed
  void invalidate(fir::Function &f) { infos.erase(&f); }
};

// What the advisor knows about the caller. CFG and dominators are only built
// when a call site reaches a rule that needs them, and the per block answers
// are computed once. The caller must not change while the context is in use.
class InlineCallerCtx {
  fir::Function &func_;
  size_t base_n_instrs_;
  size_t pending_growth_ = 0;
  CFG *cfg_ = nullptr;
  Dominators *dom_ = nullptr;
  // per block: all returns are strictly dominated by it
  TVec<bool> always_executed_;

  void ensure_analyses() {
    if (cfg_ == nullptr) {
      cfg_ = &AnalysisManager::cfg(func_);
      dom_ = &AnalysisManager::dom(func_);
    }
  }

  void compute_always_executed() {
    ensure_analyses();
    const auto &dom = *dom_;
    const size_t n_bbs = func_.n_bbs();
    // number of returns each block strictly dominates
    TVec<u32> dominated_returns(n_bbs, 0);
    u32 n_returns = 0;
    u32 n_unreachable_returns = 0;
    TVec<bool> is_unreachable_return(n_bbs, false);
    for (u32 r = 0; r < n_bbs; r++) {
      if (!func_.basic_blocks[r]->get_terminator()->is(
              fir::InstrType::ReturnInstr)) {
        continue;
      }
      n_returns++;
      if (!dom.is_reachable(r)) {
        // dominated by every other block
        n_unreachable_returns++;
        is_unreachable_return[r] = true;
        continue;
      }
      if (r == cfg_->entry) {
        continue;
      }
      u32 b = dom.idom(r);
      while (true) {
        dominated_returns[b]++;
        if (b == cfg_->entry) {
          break;
        }
        b = dom.idom(b);
      }
    }
    always_executed_.assign(n_bbs, false);
    for (u32 bb = 0; bb < n_bbs; bb++) {
      if (bb == cfg_->entry) {
        always_executed_[bb] = true;
        continue;
      }
      const u32 n_dominated =
          dominated_returns[bb] +
          (n_unreachable_returns - (is_unreachable_return[bb] ? 1 : 0));
      always_executed_[bb] = n_dominated == n_returns;
    }
  }

 public:
  explicit InlineCallerCtx(fir::Function &func)
      : func_(func), base_n_instrs_(func.n_instrs()) {}

  [[nodiscard]] fir::Function &func() const { return func_; }
  // size when the round started
  [[nodiscard]] size_t base_n_instrs() const { return base_n_instrs_; }
  // size including the callees accepted earlier in this round
  [[nodiscard]] size_t n_instrs() const {
    return base_n_instrs_ + pending_growth_;
  }
  void add_growth(size_t n) { pending_growth_ += n; }

  [[nodiscard]] bool is_always_executed(fir::BasicBlock bb) {
    if (always_executed_.empty()) {
      compute_always_executed();
    }
    return always_executed_[func_.bb_id(bb)];
  }

  // no predecessor is dominated by the block, i.e. the block is not in a loop
  [[nodiscard]] bool is_in_straightline_section(fir::BasicBlock bb) {
    ensure_analyses();
    const auto bb_id = static_cast<u32>(func_.bb_id(bb));
    for (auto pred : cfg_->bbrs[bb_id].pred) {
      if (dom_->strict_dominates(bb_id, pred)) {
        return false;
      }
    }
    return true;
  }
};

class AlwaysInlineAdvisor {
 public:
  void begin_round() {}

  [[nodiscard]] bool should_be_inlined(const fir::Instr instr,
                                       InlineCallerCtx & /*caller*/,
                                       InlineCalleeInfos &callees,
                                       InlineConfig &conf) {
    auto called_func = instr->get_arg(0);
    auto self_func = instr->get_parent()->get_parent();
    if (!called_func.is_constant() || !called_func.as_constant()->is_func()) {
      return false;
    }
    auto v = called_func.as_constant()->as_func();
    if (self_func == v && !conf.allow_recursive) {
      return false;
    }
    return !callees.get(*v.func, false).never;
  }
};

class BaseInlineAdvisor {
  u32 n_inlined_instructions = 0;
  u32 n_inlined_calls = 0;

  static constexpr bool debug_print = false;

 public:
  void begin_round() {}

  [[nodiscard]] bool should_be_inlined(const fir::Instr instr,
                                       InlineCallerCtx &caller,
                                       InlineCalleeInfos &callees,
                                       InlineConfig &conf) {
    auto called_func = instr->get_arg(0);
    if (!called_func.is_constant() || !called_func.as_constant()->is_func()) {
      return false;
    }
    auto v = called_func.as_constant()->as_func();
    // the size of the function being visited changes while it is inlined into
    const bool is_self = &caller.func() == v.func;
    const auto &info = callees.get(*v.func, true);
    if (info.never || (is_self && !conf.allow_recursive)) {
      return false;
    }
    const size_t called_n_instrs =
        is_self ? caller.base_n_instrs() : info.n_instrs;
    if (_should_be_inlined(instr, v, info, called_n_instrs, caller, conf)) {
      n_inlined_calls += 1;
      n_inlined_instructions += static_cast<u32>(called_n_instrs);
      caller.add_growth(called_n_instrs);
      return true;
    }
    return false;
  }

 private:
  [[nodiscard]] bool _should_be_inlined(const fir::Instr instr,
                                        fir::FunctionR v,
                                        const InlineCallee &info,
                                        size_t called_n_instrs,
                                        InlineCallerCtx &caller,
                                        InlineConfig &conf) const {
    auto self_func = instr->get_parent()->get_parent();
    const size_t self_n_instrs = caller.n_instrs();

    if (v->attribs.must_inline) {
      if (debug_print) {
        fmt::println("Y Must inline");
      }
      return true;
    }

    if (called_n_instrs <= (4 + info.n_args + (instr->has_result() ? 4 : 0))) {
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
    if (conf.allow_builtins) {
      if (v->name.starts_with("_ZSt3min") || v->name.starts_with("_ZSt3max") ||
          v->name.starts_with("_ZSt3absf")) {
        if (debug_print) {
          fmt::println("Y Special");
        }
        return true;
      }
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

    if (self_func->basic_blocks.size() == 1 && info.n_bbs == 1 &&
        self_n_instrs <= 5 && called_n_instrs <= 5) {
      if (debug_print) {
        fmt::println("Y self really small");
      }
      return true;
    }

    // only caller, the original gets removed by GDCE so nothing grows
    if (self_func != v && v->get_n_uses() == 1 &&
        (v->attribs.linkage == fir::Linkage::Internal ||
         v->attribs.linkage == fir::Linkage::LinkOnceODR)) {
      if (debug_print) {
        fmt::println("Y single use");
      }
      return true;
    }

    if (info.n_bbs == 1) {
      if (caller.is_always_executed(instr->get_parent())) {
        if (debug_print) {
          fmt::println("Y straight so always on hot parth");
        }
        return true;
      }
      if (called_n_instrs < 30 &&
          caller.is_in_straightline_section(instr->get_parent())) {
        if (debug_print) {
          fmt::println("Y straight and short");
        }
        return true;
      }
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

    if ((info.n_bbs == 1 && called_n_instrs < 20) || called_n_instrs < 5 ||
        (self_func->name == "main" && called_n_instrs < 10)) {
      if (debug_print) {
        fmt::println("Y short");
      }
      return true;
    }

    // The callee size vs threshold.
    // Constant arguments let branches fold and allocas let loads/stores be
    // forwarded and removed after inlining so both make the callee effectively
    // cheaper
    if (!v->attribs.cold && !v->attribs.no_return && self_func != v) {
      i64 threshold = 20;
      for (size_t i = 1; i < instr->args.size(); i++) {
        auto arg = instr->args[i];
        if (arg.is_constant()) {
          threshold += 6;
        } else if (points_into_alloca(arg)) {
          // stack object member, inlining lets the object be split up and
          // promoted since the pointer no longer escapes into the call
          threshold += 100;
        }
      }
      if (debug_print) {
        fmt::println("Score {} <= {}", called_n_instrs, threshold);
      }
      if (static_cast<i64>(called_n_instrs) <= threshold) {
        return true;
      }
    }

    if (debug_print) {
      fmt::println("N end");
    }
    return false;
  }
};

template <typename T>
concept InlineAdvisor =
    requires(T v, fir::Instr instr, InlineCallerCtx &caller,
             InlineCalleeInfos &callees, InlineConfig &conf) {
      {
        v.should_be_inlined(instr, caller, callees, conf)
      } -> std::same_as<bool>;
      v.begin_round();
    };

template <InlineAdvisor Advisor = BaseInlineAdvisor>
class Inline final : public ModulePass {
 public:
  using Config = InlineConfig;
  Config config;

  // Calls that come out of inlined bodies are decided in the same visit,
  // this bounds how deep that goes.
  static constexpr u32 MaxRounds = 8;

  PreservedAnalysis apply(fir::Context &ctx, JobSheduler * /*shed*/,
                          AnalysisManager &analyMan) override {
    ZoneScopedNC("INLINE", COLOR_OPTIMM);
    // callees are final before their callers are visited
    CallSCCs sccs{ctx};
    InlineCalleeInfos callees;
    for (auto *f : sccs.order) {
      apply(*f, sccs, callees, analyMan);
    }
    return PreservedAnalysis::all();
  }

  void apply(fir::Function &func, const CallSCCs &sccs,
             InlineCalleeInfos &callees, AnalysisManager &analyMan) {
    Advisor adv;
    TVec<fir::Instr> calls;
    // call sites that were already decided, they are not looked at again
    TSet<fir::Instr> seen;
    const u32 own_scc = sccs.scc_of.at(&func);
    const bool in_recursive_scc = sccs.is_recursive[own_scc];

    size_t first_bb = 0;
    for (u32 round = 0; round < MaxRounds; round++) {
      calls.clear();
      const size_t n_bbs_before = func.n_bbs();
      bool has_candidate = false;
      for (size_t bb_id = first_bb; bb_id < n_bbs_before && !has_candidate;
           bb_id++) {
        for (auto instr : func.basic_blocks[bb_id]->instructions) {
          if (instr->is(fir::InstrType::CallInstr) &&
              instr->args[0].is_constant() &&
              instr->args[0].as_constant()->is_func()) {
            has_candidate = true;
            break;
          }
        }
      }
      if (!has_candidate) {
        break;
      }
      InlineCallerCtx caller{func};
      adv.begin_round();

      for (size_t bb_id = first_bb; bb_id < n_bbs_before; bb_id++) {
        auto bb = func.basic_blocks[bb_id];
        for (auto instr : bb->instructions) {
          if (!instr->is(fir::InstrType::CallInstr) ||
              !seen.insert(instr).second) {
            continue;
          }
          // inside a recursive SCC callees are not final, only calls of the
          // original body are inlined (one level per pass run)
          if (round > 0 && in_recursive_scc && instr->args[0].is_constant() &&
              instr->args[0].as_constant()->is_func()) {
            auto it =
                sccs.scc_of.find(instr->args[0].as_constant()->as_func().func);
            if (it != sccs.scc_of.end() && it->second == own_scc) {
              continue;
            }
          }
          if (adv.should_be_inlined(instr, caller, callees, config)) {
            calls.push_back(instr);
          }
        }
      }
      if (calls.empty()) {
        break;
      }
      first_bb = n_bbs_before;
      bool changed = false;
      for (auto call : calls) {
        if (inline_call(call)) {
          changed = true;
          utils::StatCollector::get().addi(
              1, round == 0 ? "InlinedCalls" : "InlinedCallsRetry",
              utils::StatCollector::StatType::StatFOptim);
        }
      }
      if (changed) {
        analyMan.touched_body(func);
        callees.invalidate(func);
      }
    }
  }
};

}  // namespace foptim::optim
