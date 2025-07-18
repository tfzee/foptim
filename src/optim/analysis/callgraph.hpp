#pragma once
#include "ir/context.hpp"
#include "ir/function_ref.hpp"
#include "ir/instruction_data.hpp"
#include "utils/map.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

class CallGraph {
public:
  struct Result {
    bool indirect_calls = false;
    TVec<fir::Function *> targets;
  };
  TMap<fir::Function *, Result> call_graph;

  CallGraph(fir::Context &ctx) { update(ctx); }

  void update(fir::Context &ctx) {
    ZoneScopedN("CallGraph::update");

    for (auto &f : ctx->storage.functions) {
      for (auto bb : f.second->basic_blocks) {
        for (auto instr : bb->instructions) {
          if (instr->is(fir::InstrType::CallInstr)) {
            if (instr->args[0].is_constant() &&
                instr->args[0].as_constant()->is_func()) {
              auto target_f = instr->args[0].as_constant()->as_func();
              call_graph[f.second.get()].targets.push_back(target_f.func);
            } else {
              call_graph[f.second.get()].indirect_calls = true;
            }
          }
        }
      }
    }
  }
};
} // namespace foptim::optim
