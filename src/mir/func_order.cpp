#include "func_order.hpp"

#include <algorithm>

#include "config/compiler_config.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "utils/map.hpp"

namespace foptim::fmir {
namespace {

// Calls inside loops count 10^depth times, capped so the weights stay small
constexpr u32 MAX_LOOP_WEIGHT_DEPTH = 4;
// Do not grow clusters past this many FIR instructions, a cluster that does not
// fit into the instruction cache gives prob no further locality benefit
constexpr u64 MAX_CLUSTER_SIZE = 2048;

struct FuncInfo {
  fir::Function *func;
  // number of FIR instructions (lower bound proxy for the code size)
  u64 size = 1;
  // weighted number of direct calls to da function
  u64 heat = 0;
  bool cold = false;
  // weighted direct calls into this function, per caller
  TMap<u32, u64> callers;
};

struct Cluster {
  TVec<u32> funcs;
  u64 size = 0;
  u64 heat = 0;

  [[nodiscard]] constexpr bool denser_than(const Cluster &o) const {
    return static_cast<unsigned __int128>(heat) * o.size >
           static_cast<unsigned __int128>(o.heat) * size;
  }
};

u64 loop_weight(u32 depth) {
  u64 weight = 1;
  for (u32 i = 0; i < std::min(depth, MAX_LOOP_WEIGHT_DEPTH); i++) {
    weight *= 10;
  }
  return weight;
}

// Adds the weighted direct calls made by infos[caller_id] to the "heat" and
// "callers" of the callees
void collect_calls(TVec<FuncInfo> &infos,
                   const TMap<fir::Function *, u32> &func_ids, u32 caller_id) {
  auto *func = infos[caller_id].func;
  optim::CFG cfg{*func};
  optim::Dominators dom{cfg};
  optim::LoopInfoAnalysis loops{dom};

  TVec<u32> loop_depth(cfg.bbrs.size(), 0);
  for (const auto &loop : loops.info) {
    for (auto bb_id : loop.body_nodes) {
      loop_depth[bb_id] += 1;
    }
  }

  for (u32 bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    const auto weight = loop_weight(loop_depth[bb_id]);
    for (auto instr : cfg.bbrs[bb_id].bb->instructions) {
      if (!instr->is(fir::InstrType::CallInstr) ||
          !instr->args[0].is_constant() ||
          !instr->args[0].as_constant()->is_func()) {
        continue;
      }
      auto *target = instr->args[0].as_constant()->as_func().func;
      auto target_id = func_ids.find(target);
      // declarations and self recursion tell us nothing about the layout
      if (target_id == func_ids.end() || target_id->second == caller_id) {
        continue;
      }
      infos[target_id->second].heat += weight;
      infos[target_id->second].callers[caller_id] += weight;
    }
  }
}

} // namespace

// Roughly based on the C3 algorithm (Ottoni & Maher, "Optimizing Function
// Placement for Large-Scale Data-Center Applications"):
//  1. Walk the functions by decreasing density (calls / size) and append the
//     cluster of each one to the cluster of its heaviest caller. Callees then
//     directly follow their callers.
//  2. Emit clusters by decreasing density.
// Functions without direct calls keep their input order after the hot ones and
// cold / noreturn functions come last. Declarations produce no code so they are
// added at the very end.
// Everything is tie broken by the input order so the result is deterministic.
TVec<fir::Function *> get_lowering_order(fir::Context &ctx) {
  const auto &all_funcs = ctx->storage.functions.all();
  if (ctx.config->debug.no_reorder_funcs) {
    return {all_funcs.begin(), all_funcs.end()};
  }

  TVec<FuncInfo> infos;
  TVec<fir::Function *> decls;
  TMap<fir::Function *, u32> func_ids;
  for (auto *func : all_funcs) {
    if (func->is_decl()) {
      decls.push_back(func);
      continue;
    }
    func_ids.insert({func, static_cast<u32>(infos.size())});
    infos.push_back({.func = func,
                     .size = std::max<u64>(1, func->n_instrs()),
                     .cold = func->attribs.cold || func->attribs.no_return,
                     .callers = {}});
  }
  for (u32 id = 0; id < infos.size(); id++) {
    collect_calls(infos, func_ids, id);
  }

  TVec<Cluster> clusters(infos.size());
  TVec<u32> cluster_of(infos.size());
  for (u32 id = 0; id < infos.size(); id++) {
    clusters[id] = {
        .funcs = {id}, .size = infos[id].size, .heat = infos[id].heat};
    cluster_of[id] = id;
  }

  TVec<u32> by_density;
  for (u32 id = 0; id < infos.size(); id++) {
    if (!infos[id].cold && infos[id].heat > 0) {
      by_density.push_back(id);
    }
  }
  std::ranges::stable_sort(by_density, [&](u32 a, u32 b) {
    return clusters[a].denser_than(clusters[b]);
  });

  for (auto id : by_density) {
    // heaviest caller, ties go to the earlier function
    u32 best_caller = id;
    u64 best_weight = 0;
    for (const auto &[caller, weight] : infos[id].callers) {
      if (infos[caller].cold) {
        continue;
      }
      if (weight > best_weight ||
          (weight == best_weight && caller < best_caller)) {
        best_caller = caller;
        best_weight = weight;
      }
    }
    if (best_weight == 0) {
      continue;
    }
    auto &target = clusters[cluster_of[best_caller]];
    auto &moved = clusters[cluster_of[id]];
    if (&target == &moved || target.size + moved.size > MAX_CLUSTER_SIZE) {
      continue;
    }
    const auto moved_id = cluster_of[id];
    for (auto f : moved.funcs) {
      cluster_of[f] = cluster_of[best_caller];
    }
    target.funcs.insert(target.funcs.end(), moved.funcs.begin(),
                        moved.funcs.end());
    target.size += moved.size;
    target.heat += moved.heat;
    clusters[moved_id] = {};
  }

  // hot clusters by density. Then everything without calls. Then cold code
  TVec<u32> hot_clusters;
  TVec<u32> warm;
  TVec<u32> cold;
  for (u32 id = 0; id < infos.size(); id++) {
    if (infos[id].cold) {
      cold.push_back(id);
    } else if (clusters[id].funcs.empty()) {
      continue;
    } else if (clusters[id].heat > 0) {
      hot_clusters.push_back(id);
    } else {
      warm.push_back(id);
    }
  }
  // clusters are identified by the id of their head, ties keep that order
  std::ranges::stable_sort(hot_clusters, [&](u32 a, u32 b) {
    return clusters[a].denser_than(clusters[b]);
  });

  TVec<fir::Function *> order;
  order.reserve(all_funcs.size());
  for (auto cluster_id : hot_clusters) {
    for (auto f : clusters[cluster_id].funcs) {
      order.push_back(infos[f].func);
    }
  }
  for (auto id : warm) {
    order.push_back(infos[id].func);
  }
  for (auto id : cold) {
    order.push_back(infos[id].func);
  }
  order.insert(order.end(), decls.begin(), decls.end());
  return order;
}

} // namespace foptim::fmir
