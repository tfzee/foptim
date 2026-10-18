#pragma once
#include <algorithm>

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
    TVec<fir::Function *> targeted_by;
  };
  TMap<fir::Function *, Result> call_graph;

  CallGraph(fir::Context &ctx) { update(ctx); }

  void update(fir::Context &ctx) {
    ZoneScopedN("CallGraph::update");

    for (auto *f : ctx->storage.functions.all()) {
      for (auto bb : f->basic_blocks) {
        for (auto instr : bb->instructions) {
          if (instr->is(fir::InstrType::CallInstr)) {
            if (instr->args[0].is_constant() &&
                instr->args[0].as_constant()->is_func()) {
              auto target_f = instr->args[0].as_constant()->as_func();
              call_graph[target_f.func].targeted_by.push_back(f);
              call_graph[f].targets.push_back(target_f.func);
            } else {
              call_graph[f].indirect_calls = true;
            }
          }
        }
      }
    }
  }
};

// Strongly connected components of the direct call graph (declarations are
// ignored) in callees first order. The order follows the function insertion
// order, so it stays consistent.
struct CallSCCs {
  // functions in callees first order, members of one SCC are adjacent
  TVec<fir::Function *> order;
  // SCC index per function, in the same callees first order
  TMap<fir::Function *, u32> scc_of;
  TVec<bool> is_recursive;

  explicit CallSCCs(fir::Context &ctx) {
    ZoneScopedN("CallSCCs");
    const size_t n_funcs = ctx->storage.functions.all().size();
    TVec<fir::Function *> funcs;
    TMap<fir::Function *, u32> index_of;
    funcs.reserve(n_funcs);
    index_of.reserve(n_funcs);
    for (auto *f : ctx->storage.functions.all()) {
      if (!f->is_decl()) {
        index_of[f] = static_cast<u32>(funcs.size());
        funcs.push_back(f);
      }
    }
    const u32 n = static_cast<u32>(funcs.size());
    order.reserve(n);
    scc_of.reserve(n);
    is_recursive.reserve(n);
    TVec<TVec<u32>> callees(n);
    TVec<bool> self_call(n, false);
    for (u32 i = 0; i < n; i++) {
      for (auto bb : funcs[i]->basic_blocks) {
        for (auto instr : bb->instructions) {
          if (instr->is(fir::InstrType::CallInstr) &&
              instr->args[0].is_constant() &&
              instr->args[0].as_constant()->is_func()) {
            auto it =
                index_of.find(instr->args[0].as_constant()->as_func().func);
            if (it != index_of.end()) {
              callees[i].push_back(it->second);
              self_call[i] = self_call[i] || it->second == i;
            }
          }
        }
      }
    }

    // iterative Tarjan, SCCs are completed callees first
    constexpr u32 None = ~0U;
    TVec<u32> idx(n, None);
    TVec<u32> low(n, 0);
    TVec<bool> on_stack(n, false);
    TVec<u32> stack;
    stack.reserve(n);
    struct Frame {
      u32 node;
      u32 next_edge;
    };
    TVec<Frame> call_stack;
    call_stack.reserve(n);
    u32 counter = 0;
    for (u32 root = 0; root < n; root++) {
      if (idx[root] != None) {
        continue;
      }
      call_stack.push_back({.node = root, .next_edge = 0});
      idx[root] = low[root] = counter++;
      stack.push_back(root);
      on_stack[root] = true;
      while (!call_stack.empty()) {
        auto &fr = call_stack.back();
        const u32 v = fr.node;
        if (fr.next_edge < callees[v].size()) {
          const u32 w = callees[v][fr.next_edge++];
          if (idx[w] == None) {
            idx[w] = low[w] = counter++;
            stack.push_back(w);
            on_stack[w] = true;
            call_stack.push_back({.node = w, .next_edge = 0});
          } else if (on_stack[w]) {
            low[v] = std::min(low[v], idx[w]);
          }
          continue;
        }
        if (low[v] == idx[v]) {
          const u32 scc = static_cast<u32>(is_recursive.size());
          const size_t first = order.size();
          u32 w = None;
          do {
            w = stack.back();
            stack.pop_back();
            on_stack[w] = false;
            order.push_back(funcs[w]);
            scc_of[funcs[w]] = scc;
          } while (w != v);
          // members come off the stack in reverse discovery order
          std::reverse(order.begin() + static_cast<i64>(first), order.end());
          is_recursive.push_back(order.size() - first > 1 || self_call[v]);
        }
        call_stack.pop_back();
        if (!call_stack.empty()) {
          const u32 parent = call_stack.back().node;
          low[parent] = std::min(low[parent], low[v]);
        }
      }
    }
  }
};
}  // namespace foptim::optim
