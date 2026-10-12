#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include "config/compiler_config.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/function_pass.hpp"
#include "utils/arena.hpp"

namespace foptim::optim {

class SLPVectorizer final : public FunctionPass {
public:
  struct Config {
    bool reductions = true;
  } config;
  static constexpr bool debug_print = false;
  // whole function vectorization of a call clones the body of the callee
  static constexpr bool InterproceduralRead = true;

  struct SeedInstrData {
    fir::Instr instr;
    fir::ValueR a;
    fir::ValueR b;
  };
  struct SeedBundle {
    fir::ValueR base;
    fir::TypeR type;
    //(base*a + b)
    TVec<SeedInstrData> data;
  };
  // How the lanes of a node relate to each other. Only `Uniform` nodes could
  // stay scalar, `Consecutive` is a unit stride access of adjacent memory.
  enum class LaneShape : u8 { Uniform, Consecutive, Varying };
  enum class NodeKind : u8 {
    Broadcast,
    Gather,
    HorizRed,
    Binary,
    Zext,
    ITrunc,
    Call,
    Unary,
    Extract,
    Store,
    Intrin,
    Constant,
    Conversion,
    Select,
    Load,
  };
  // One widened value of a plan. Plain data, nodes refer to each other by index
  // into `Plan::nodes` (operand order: children are in reverse operand order,
  // the worklist is LIFO). Only lives in the temp arena.
  struct PlanNode {
    NodeKind kind = NodeKind::Broadcast;
    LaneShape shape = LaneShape::Varying;
    bool after = false;
    u32 n_lanes = 0;
    // instruction the widened code is built relative to
    fir::Instr insert_loc;
    TVec<u32> children;
    // scalar result type of a lane (casts, select, binary, ...)
    fir::TypeR type;
    // sub type of the scalar instruction (binary, unary, conversion, intrin)
    u32 sub = 0;
    // broadcast source, callee of a call
    fir::ValueR value;
    // store / load: first lane
    fir::Instr aux;
    // constant / gather: scalar value of every lane
    TVec<fir::ValueR> lanes;
  };
  // result of planning a bundle, `nodes[0]` is the root
  struct Plan {
    TVec<PlanNode> nodes;
  };

  // what a seed provider looks at: one block, it is asked about every
  // instruction in order
  struct SeedRegion {
    fir::BasicBlock bb;
    AliasAnalyis &aa;
    const conf::CompConf &conf;
    bool reductions;
  };
  class SeedProvider {
  public:
    TVec<SeedBundle> bundles;
    virtual void visit(SeedRegion &region, size_t instr_id) = 0;
    // sort / trim / drop unusable bundles after the scan
    virtual void finalize(const conf::CompConf &conf) = 0;
    virtual ~SeedProvider() = default;
  };

  static std::pair<SeedInstrData, fir::ValueR>
  get_storeload_data(fir::Instr storeload);

  static std::optional<SeedBundle>
  find_successive_loads(fir::BasicBlock bb, size_t instr_id, AliasAnalyis &aa);

  static std::optional<SeedBundle>
  find_reduction(fir::BasicBlock bb, size_t instr_id, AliasAnalyis &aa,
                 const conf::CompConf &conf);

  static std::optional<SeedBundle>
  find_successive_stores(fir::BasicBlock bb, size_t instr_id, AliasAnalyis &aa);

private:

  void find_seeds(const conf::CompConf &conf, fir::BasicBlock bb,
                  TVec<SeedBundle> &store_bundles,
                  TVec<SeedBundle> &load_bundles,
                  TVec<SeedBundle> &reduction_bundles, AliasAnalyis &aa);

  bool constant_vector_store(fir::Context &ctx, SeedBundle &bundle);

  void continious_vector_store(SeedBundle &bundle, fir::ValueR value);
  // Failed: no valid tree, NotWorth: valid tree but not profitable (both leave
  // the IR untouched), Applied: the IR was rewritten
  enum class VecResult : u8 { Failed, NotWorth, Applied };
  bool build_plan(fir::Context &ctx, const conf::Target::Features &features,
                  SeedBundle &bundle, const TVec<SeedBundle> &load_bundles,
                  bool reduction, Plan &plan);
  VecResult tree_vectorize(fir::Context &ctx, SeedBundle &bundle,
                           const TVec<SeedBundle> &load_bundles,
                           bool reduction = false);

public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("SLPVectorizer");
    AliasAnalyis aa{};
    // if (func.name != "_Z19_nettle_aes_encryptjPKjPK9aes_tablemPhPKh") {
    //   return;
    // }
    // fmt::println("{:cd}", func);
    TVec<SeedBundle> store_bundles;
    TVec<SeedBundle> load_bundles;
    TVec<SeedBundle> reduction_bundles;
    for (auto bb : func.basic_blocks) {
      find_seeds(*ctx.config, bb, store_bundles, load_bundles,
                 reduction_bundles, aa);
    }
    if (store_bundles.empty() && reduction_bundles.empty()) {
      return PreservedAnalysis::all();
    }
    if constexpr (debug_print) {
      fmt::println("Stor {} Lod {} Red {}", store_bundles.size(),
                   load_bundles.size(), reduction_bundles.size());
    }

    bool modified = false;
    for (auto &b : store_bundles) {
      bool already_used = false;
      for (const auto &data : b.data) {
        if (!data.instr.is_valid()) {
          already_used = true;
          break;
        }
      }
      if (already_used) {
        continue;
      }
      // if (constant_vector_store(ctx, b)) {
      //   continue;
      // }
      auto stor = utils::TempAlloc<void *>::save();
      auto res = tree_vectorize(ctx, b, load_bundles);
      if (res != VecResult::Failed) {
        modified |= res == VecResult::Applied;
        utils::TempAlloc<void *>::restore(stor);
        continue;
      }
    }
    for (auto &b : reduction_bundles) {
      bool already_used = false;
      for (const auto &data : b.data) {
        if (!data.instr.is_valid()) {
          already_used = true;
          break;
        }
      }
      if (already_used) {
        continue;
      }
      auto stor = utils::TempAlloc<void *>::save();
      auto res = tree_vectorize(ctx, b, load_bundles, true);
      if (res != VecResult::Failed) {
        modified |= res == VecResult::Applied;
        utils::TempAlloc<void *>::restore(stor);
        continue;
      }
    }

    // only instructions change, never blocks or edges
    return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
  }
};

} // namespace foptim::optim
