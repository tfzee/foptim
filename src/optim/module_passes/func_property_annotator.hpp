#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/callgraph.hpp"
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
        if (instr->is(fir::InstrType::StoreInstr)) {
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
            if (!f->no_recurse) {
              // this is not quite true
              // subfunction can recurse aslong as this recursion does not go
              // through the current function
              r.wont_recurse = false;
            }
            if (f->mem_read_none) {
            } else if (f->mem_read_only) {
              r.does_read = true;
            } else if (f->mem_write_only) {
              r.does_write = true;
            } else {
              r.does_read = true;
              r.does_write = true;
            }
          }
        }
        if (r.does_read && r.does_write && !r.wont_recurse) {
          return r;
        }
      }
    }
    return r;
  }

  void apply(fir::Context &ctx, JobSheduler * /*unused*/) override {
    ZoneScopedNC("FuncPropAnnotator", COLOR_OPTIMF);
    CallGraph call_graph{ctx};
    AliasAnalyis aa;

    TVec<fir::Function *> worklist;
    for (auto &f : ctx->storage.functions) {
      if (!f.second->is_decl()) {
        worklist.push_back(f.second.get());
      } else {
        // TODO: some builtins could bemarked here
      }
    }

    while (!worklist.empty()) {
      auto *v = worklist.back();
      worklist.pop_back();
      aa.reset();
      auto r = apply(v, call_graph, aa);
      bool modified = false;
      if (r.wont_recurse && !v->no_recurse) {
        v->no_recurse = true;
        modified = true;
      }
      if (r.does_read && !r.does_write && !v->mem_read_only) {
        modified = true;
        v->mem_read_none = false;
        v->mem_read_only = true;
        v->mem_write_only = false;
      } else if (!r.does_read && r.does_write && !v->mem_write_only) {
        modified = true;
        v->mem_read_none = false;
        v->mem_read_only = false;
        v->mem_write_only = true;
      } else if (!r.does_read && !r.does_write && !v->mem_read_none) {
        modified = true;
        v->mem_read_none = true;
        v->mem_read_only = false;
        v->mem_write_only = false;
      }
      if (modified) {
        // fmt::println("MODIFIED {:d}", *v);
        for (auto *v : call_graph.call_graph[v].targeted_by) {
          worklist.push_back(v);
        }
      }
    }
  }
};
}  // namespace foptim::optim
