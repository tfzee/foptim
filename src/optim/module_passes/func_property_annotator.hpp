#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/callgraph.hpp"
#include "optim/analysis/mem/alloca_escape.hpp"
#include "optim/analysis/mem/basic_alias_test.hpp"
#include "optim/analysis/mem/ptr_arg_use.hpp"
#include "optim/helper/WFVector.hpp"
#include "optim/helper/helper.hpp"
#include "optim/module_pass.hpp"

namespace foptim::optim {

class FuncPropAnnotator final : public ModulePass {
 public:
  struct Result {
    bool does_read = false;
    bool does_write = false;
    bool wont_recurse = true;
  };

  Result apply(fir::Function *func, CallGraph &cg, AliasAnalyis &aa) {
    Result r{};
    if (cg.call_graph[func].indirect_calls) {
      return {.does_read = true, .does_write = true, .wont_recurse = false};
    }

    for (auto bb : func->basic_blocks) {
      for (auto instr : bb->instructions) {
        // would need to do a proper AA to see if its local
        if (instr->is(fir::InstrType::StoreInstr) ||
            instr->is(fir::IntrinsicSubType::Memset)) {
          if (!aa.is_known_local_stack(instr->args[0])) {
            r.does_write = true;
          }
        } else if (instr->is(fir::InstrType::LoadInstr)) {
          if (!aa.is_known_local_stack(instr->args[0])) {
            r.does_read = true;
          }
        } else if (instr->is(fir::InstrType::AtomicRMW)) {
          if (!aa.is_known_local_stack(instr->args[0])) {
            r.does_write = true;
            r.does_read = true;
          }
        } else if (instr->is(fir::IntrinsicSubType::Memcmp)) {
          if (!aa.is_known_local_stack(instr->args[0]) ||
              !aa.is_known_local_stack(instr->args[1])) {
            r.does_read = true;
          }
        } else if (instr->is(fir::IntrinsicSubType::Memcpy) ||
                   instr->is(fir::IntrinsicSubType::Memmove)) {
          if (!aa.is_known_local_stack(instr->args[0])) {
            r.does_write = true;
          }
          if (!aa.is_known_local_stack(instr->args[1])) {
            r.does_read = true;
          }
        } else if (instr->is(fir::InstrType::CallInstr)) {
          // indirect calls already handled
          if (instr->args[0].is_constant() &&
              instr->args[0].as_constant()->is_func()) {
            auto f = instr->args[0].as_constant()->as_func();
            if (f == func) {
              r.wont_recurse = false;
              // can skip r/w analysis since if we hit it otherwise in this run
              // then we know its written otherwise we know it wont
              continue;
            }
            if (!f->attribs.no_recurse) {
              // this is not quite true
              // subfunction can recurse aslong as this recursion does not go
              // through the current function
              r.wont_recurse = false;
            }
            if (f->attribs.mem_read_none) {
            } else if (f->attribs.mem_read_only) {
              r.does_read = true;
            } else if (f->attribs.mem_write_only) {
              r.does_write = true;
            } else {
              r.does_read = true;
              r.does_write = true;
            }
          }
        } else if (instr->pot_modifies_mem() || instr->pot_reads_mem()) {
          r.does_write = instr->pot_modifies_mem();
          r.does_read = instr->pot_reads_mem();
        }
        if (r.does_read && r.does_write && !r.wont_recurse) {
          return r;
        }
      }
    }
    return r;
  }

  // every value returned as result r is null or a pointer into the noalias
  // result of a call that reaches nothing else than this result
  static bool returns_fresh(
      const TVec<fir::Instr> &rets, u32 r,
      const TMap<fir::ValueR, TVec<fir::ValueR>> &incoming) {
    TVec<fir::ValueR> work;
    TSet<fir::ValueR> seen;
    TVec<fir::ValueR> sources;
    for (auto ret : rets) {
      work.push_back(ret->args[r]);
    }
    while (!work.empty()) {
      const auto v = work.back();
      work.pop_back();
      if (!seen.insert(v).second) {
        continue;
      }
      if (v.is_constant()) {
        if (!v.as_constant()->is_null()) {
          return false;
        }
        continue;
      }
      if (v.is_bb_arg()) {
        const auto it = incoming.find(v);
        if (it == incoming.end()) {
          return false;
        }
        for (auto in : it->second) {
          work.push_back(in);
        }
        continue;
      }
      if (!v.is_instr()) {
        return false;
      }
      const auto instr = v.as_instr();
      if (instr->is(fir::InstrType::SelectInstr)) {
        work.push_back(instr->args[1]);
        work.push_back(instr->args[2]);
      } else if (instr->is(fir::BinaryInstrSubType::PtrAdd)) {
        work.push_back(instr->args[0]);
      } else if (instr->is(fir::InstrType::CallInstr) &&
                 instr->args[0].is_constant() &&
                 instr->args[0].as_constant()->is_func() &&
                 instr->args[0].as_constant()->as_func()->result_noalias(
                     v.result_idx())) {
        sources.push_back(v);
      } else {
        return false;
      }
    }
    const EscapeOptions opts{.returned_as = static_cast<i32>(r),
                             .through_block_args = true};
    return std::ranges::none_of(sources, [&](fir::ValueR s) {
      return analyze_alloca_escape(s, opts).escapes;
    });
  }

  // noalias and nonnull of the pointer results of a definition, only ever
  // added. a definition that may be replaced at link time says nothing
  static bool deduce_result_attribs(fir::Function &f) {
    const auto &ft = f.func_ty->as_func();
    const u32 n = ft.n_results();
    if (n == 0 || n > 8 || f.attribs.linkage == fir::Linkage::Weak ||
        f.attribs.linkage == fir::Linkage::LinkOnce) {
      return false;
    }
    TVec<fir::Instr> rets;
    TMap<fir::ValueR, TVec<fir::ValueR>> incoming;
    for (auto bb : f.basic_blocks) {
      auto term = bb->get_terminator();
      if (term->is(fir::InstrType::ReturnInstr)) {
        rets.push_back(term);
      }
      for (const auto &target : term->bbs) {
        for (u32 j = 0; j < target.args.size(); j++) {
          incoming[fir::ValueR{target.bb->args[j]}].push_back(target.args[j]);
        }
      }
    }
    if (rets.empty()) {
      return false;
    }
    u8 na = 0;
    u8 nn = 0;
    for (u32 r = 0; r < n; r++) {
      if (!ft.return_types[r]->is_ptr()) {
        continue;
      }
      if (f.result_noalias(r) || returns_fresh(rets, r, incoming)) {
        na |= 1U << r;
      }
      if (f.result_nonnull(r) || std::ranges::all_of(rets, [&](fir::Instr ret) {
            return is_known_nonnull(ret->args[r]);
          })) {
        nn |= 1U << r;
      }
    }
    bool same = true;
    for (u32 r = 0; r < n; r++) {
      same = same && f.result_noalias(r) == (((na >> r) & 1) != 0) &&
             f.result_nonnull(r) == (((nn >> r) & 1) != 0);
    }
    if (same) {
      return false;
    }
    f.set_result_attribs(na, nn);
    return true;
  }

  // A pointer argument is nonnull if every call passes a nonnull pointer, for
  // internal functions that are only called directly.
  static void deduce_nonnull_args(fir::Context &ctx, CallGraph &cg,
                                  AnalysisManager &analyMan) {
    const auto address_taken = functions_referenced_by_globals(ctx);
    TVec<fir::Function *> worklist;
    for (auto *f : ctx->storage.functions.all()) {
      worklist.push_back(f);
    }
    while (!worklist.empty()) {
      auto *f = worklist.back();
      worklist.pop_back();
      if (f->is_decl() || f->attribs.variadic ||
          f->attribs.linkage != fir::Linkage::Internal ||
          address_taken.contains(f) || f->get_n_uses() == 0) {
        continue;
      }
      bool closed = true;
      for (auto use : f->get_uses()) {
        closed = closed && use.user->is(fir::InstrType::CallInstr) &&
                 use.type == fir::UseType::NormalArg && use.argId == 0;
      }
      if (!closed) {
        continue;
      }
      auto entry = f->get_entry();
      bool modified = false;
      for (u32 i = 0; i < entry->args.size(); i++) {
        auto arg = entry->args[i];
        if (arg->nonnull || !arg->get_type()->is_ptr()) {
          continue;
        }
        bool all_nonnull = true;
        for (auto use : f->get_uses()) {
          const auto passed = use.user->args[i + 1];
          // a recursive call passing the argument on
          const bool passes_self =
              passed.is_bb_arg() && passed.as_bb_arg() == arg;
          all_nonnull =
              all_nonnull && (passes_self || is_known_nonnull(passed));
        }
        if (all_nonnull) {
          arg->nonnull = true;
          modified = true;
        }
      }
      if (modified) {
        analyMan.touched_interface(*f);
        for (auto *callee : cg.call_graph[f].targets) {
          worklist.push_back(callee);
        }
      }
    }
  }

  PreservedAnalysis apply(fir::Context &ctx, JobSheduler * /*shed*/,
                          AnalysisManager &analyMan) override {
    ZoneScopedNC("FuncPropAnnotator", COLOR_OPTIMF);
    CallGraph call_graph{ctx};
    AliasAnalyis aa{false};

    TVec<fir::Function *> worklist;
    for (auto *f : ctx->storage.functions.all()) {
      if (!f->is_decl()) {
        worklist.push_back(f);
      } else {
        // TODO: some builtins could bemarked here
      }
    }

    while (!worklist.empty()) {
      auto *v = worklist.back();
      worklist.pop_back();
      aa.reset();
      bool modified = false;
      auto r = apply(v, call_graph, aa);
      if (r.wont_recurse && !v->attribs.no_recurse) {
        v->attribs.no_recurse = true;
        modified = true;
      }
      if (r.does_read && !r.does_write && !v->attribs.mem_read_only) {
        modified = true;
        v->attribs.mem_read_none = false;
        v->attribs.mem_read_only = true;
        v->attribs.mem_write_only = false;
      } else if (!r.does_read && r.does_write && !v->attribs.mem_write_only) {
        modified = true;
        v->attribs.mem_read_none = false;
        v->attribs.mem_read_only = false;
        v->attribs.mem_write_only = true;
      } else if (!r.does_read && !r.does_write && !v->attribs.mem_read_none) {
        modified = true;
        v->attribs.mem_read_none = true;
        v->attribs.mem_read_only = false;
        v->attribs.mem_write_only = false;
      }
      for (auto arg : v->get_entry()->args) {
        if (!arg->get_type()->is_ptr() || (arg->nocapture && arg->readonly)) {
          continue;
        }
        const auto use = analyze_ptr_arg_use(arg);
        if (use.nocapture && !arg->nocapture) {
          arg->nocapture = true;
          modified = true;
        }
        if (use.readonly && !arg->readonly) {
          arg->readonly = true;
          modified = true;
        }
      }
      if (deduce_result_attribs(*v)) {
        modified = true;
      }
      // after the memory attributes, it needs mem_read_none
      auto new_wfvec = can_whole_function_vectorize(
          *v, 0, ctx.config->target.features.avx512vpopcntdq);
      const bool can_wfvec =
          new_wfvec.has_value() && !v->func_ty->as_func().has_byval();
      if (static_cast<int>(can_wfvec) != v->attribs.maybe_can_wfvec) {
        v->attribs.maybe_can_wfvec = can_wfvec;
        modified = true;
      }
      if (modified) {
        // fmt::println("MODIFIED {:d}", *v);
        analyMan.touched_interface(*v);
        for (auto *v : call_graph.call_graph[v].targeted_by) {
          worklist.push_back(v);
        }
      }
    }
    deduce_nonnull_args(ctx, call_graph, analyMan);
    return PreservedAnalysis::all();
  }
};
}  // namespace foptim::optim
