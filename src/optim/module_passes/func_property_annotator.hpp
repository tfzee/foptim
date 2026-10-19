#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/callgraph.hpp"
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
    AliasAnalyis aa;

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
      auto new_wfvec = can_whole_function_vectorize(*v);
      if (static_cast<int>(new_wfvec.has_value()) !=
          v->attribs.maybe_can_wfvec) {
        v->attribs.maybe_can_wfvec = new_wfvec.has_value();
        modified = true;
      }
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
