#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include "config/compiler_config.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/block_mem_access.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/analysis/loop_mem_dep.hpp"
#include "optim/function_pass.hpp"
#include "utils/arena.hpp"

namespace foptim::optim {

// Program order of the instructions of one block. Vector code is emitted next
// to the program-last lane of a node, so the order has to know the
// instructions earlier bundles inserted: it is rebuilt after a bundle was
// applied. Must be refreshed (`ensure`) outside of a `TempAlloc` save/restore
// window, the map lives in the temp arena.
// an instruction result the planner can take as a lane, results of multi result
// calls are plain external values
inline bool lane_instr(fir::ValueR v) {
  return v.is_instr() && !v.as_instr().is_multi_result();
}

class BlockOrder {
 public:
  explicit BlockOrder(fir::BasicBlock bb_) : bb(bb_) {}
  void invalidate() { stale = true; }
  void ensure() {
    if (!stale) {
      return;
    }
    pos_of.clear();
    const auto &instrs = bb->instructions;
    for (u32 i = 0; i < instrs.size(); i++) {
      pos_of[key(instrs[i])] = i;
    }
    stale = false;
  }
  // position of an instruction of this block, nullopt for other blocks
  [[nodiscard]] std::optional<u32> pos(fir::Instr i) const {
    auto it = pos_of.find(key(i));
    return it == pos_of.end() ? std::nullopt : std::optional<u32>{it->second};
  }

 private:
  // a multi result call is keyed by its first result
  static fir::ValueR key(fir::Instr i) {
    return i.is_multi_result() ? i.result(0) : i.result();
  }
  fir::BasicBlock bb;
  TMap<fir::ValueR, u32> pos_of;
  bool stale = true;
};

// per block data shared by the seed providers and the plan builder
struct BlockInfo {
  fir::BasicBlock bb;
  BlockOrder order;
  std::optional<BlockMemAccesses> mem_;

  explicit BlockInfo(fir::BasicBlock bb_) : bb(bb_), order(bb_) {}
  // snapshot of the memory operations, built on first use (and so has to be
  // asked for outside of a `TempAlloc` window, see `BlockOrder`)
  BlockMemAccesses &mem() {
    if (!mem_) {
      mem_.emplace(bb);
    }
    return *mem_;
  }
};

class SLPVectorizer final : public FunctionPass {
 public:
  struct Config {
    bool reductions = true;
    // loop vectorization
    bool loops = true;
    // upper bound on the vector factor, 0 = only the target decides
    u32 max_vf = 0;
    // trip count of the cost model when it is not a constant
    u32 assumed_trip = 64;
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
    // block the bundle lives in (the block of the stores / of the base)
    BlockInfo *block = nullptr;
  };
  // How the lanes of a node relate to each other. Only `Uniform` nodes could
  // stay scalar, `Consecutive` is a unit stride access of adjacent memory.
  enum class LaneShape : u8 {
    Uniform,
    Consecutive,
    Varying,
    // loops, lane i is base plus i times stride, only kept as the scalar of
    // lane 0
    Strided,
    // loops: loop carried vector (reduction accumulator)
    Recurrence,
  };
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

  // what a seed provider looks at: one block
  struct SeedRegion {
    BlockInfo &info;
    AliasAnalyis &aa;
    const conf::CompConf &conf;
    bool reductions;
  };
  class SeedProvider {
   public:
    TVec<SeedBundle> bundles;
    virtual void scan(SeedRegion &region) = 0;
    // drop unusable bundles / trim them after the scan
    virtual void finalize(const conf::CompConf &conf) = 0;
    virtual ~SeedProvider() = default;
  };

  static std::pair<SeedInstrData, fir::ValueR> get_storeload_data(
      fir::Instr storeload);

  static std::optional<SeedBundle> find_reduction(fir::BasicBlock bb,
                                                  size_t instr_id,
                                                  const conf::CompConf &conf);

  // Loop vectorization. Lane i of a vector is iteration i of an innermost
  // single block countable loop. Planning is read only and emit_loop rewrites
  // the loop into a pre block, a vector loop and a middle block that falls
  // back to the original loop as the remainder.
  enum class LoopNodeKind : u8 {
    // header argument that is an induction variable or a reduction accumulator
    Induction,
    Reduction,
    // copied as is, computes lane 0
    Scalar,
    Load,
    Store,
    Binary,
    // Abs and FAbs
    Intrinsic,
  };
  // nodes are in program order (header arguments first), operands are indices
  struct LoopNode {
    LoopNodeKind kind = LoopNodeKind::Scalar;
    LaneShape shape = LaneShape::Uniform;
    fir::ValueR value;
    // scalar result type of one lane
    fir::TypeR type;
    // Binary / Reduction operation
    u32 sub = 0;
    // Induction: step per iteration
    i64 stride = 0;
    TVec<u32> ops;
    // Induction / Reduction: index of the header argument
    u32 arg_id = 0;
  };
  struct LoopPlan {
    TVec<LoopNode> nodes;
    TMap<fir::ValueR, u32> node_of;
    fir::BasicBlock header{fir::BasicBlock::invalid()};
    u32 back_edge = 0;
    u32 exit_edge = 0;
    // induction variable with step 1 that counts the vector iterations
    u32 counter = 0;
    // header executions (invariant, expandable)
    SCEVRef trip = InvalidSCEV;
    std::optional<i128> const_trip;
    u32 elem_bits = 0;
    u32 n_vector_ops = 0;
    u32 n_scalar_ops = 0;
    u32 n_reductions = 0;
    // checked for each VF with LoopMemDep
    TVec<LoopMemDep::Access> accesses;
    // instructions of the body without the terminator
    u32 scalar_cost = 0;
  };
  static bool loop_legal(const LoopPlan &plan, u32 vf,
                         const conf::Target::Features &features);
  // estimated cost of the whole loop, scalar_total is the cost of the original
  static i64 loop_cost(const LoopPlan &plan, u32 vf,
                       const conf::Target::Features &features, u32 assumed_trip,
                       i64 &scalar_total);
  static u32 max_vector_bits(const conf::Target::Features &features);

 private:
  bool vectorize_loops(fir::Context &ctx, fir::Function &func);
  bool build_loop_plan(fir::Context &ctx, CFG &cfg, Dominators &dom,
                       LoopInfo &loop, ScalarEvo &evo, LoopPlan &plan);
  bool emit_loop(fir::Context &ctx, fir::Function &func, CFG &cfg,
                 ScalarEvo &evo, LoopPlan &plan, u32 vf);

  void find_seeds(const conf::CompConf &conf, BlockInfo &info,
                  TVec<SeedBundle> &store_bundles,
                  TVec<SeedBundle> &reduction_bundles, AliasAnalyis &aa);

  // Failed: no valid tree, NotWorth: valid tree but not profitable (both leave
  // the IR untouched), Applied: the IR was rewritten
  enum class VecResult : u8 { Failed, NotWorth, Applied };
  bool build_plan(fir::Context &ctx, const conf::Target::Features &features,
                  SeedBundle &bundle, bool reduction, Plan &plan);
  VecResult tree_vectorize(fir::Context &ctx, SeedBundle &bundle,
                           bool reduction = false);

 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("SLPVectorizer");
    const bool loops_changed = config.loops && vectorize_loops(ctx, func);
    AliasAnalyis aa{};
    // one entry per block, must not move once bundles point into it
    TVec<BlockInfo> blocks;
    for (auto bb : func.basic_blocks) {
      blocks.emplace_back(bb);
    }
    TVec<SeedBundle> store_bundles;
    TVec<SeedBundle> reduction_bundles;
    {
      ZoneScopedN("Find Seeds");
      for (auto &info : blocks) {
        find_seeds(*ctx.config, info, store_bundles, reduction_bundles, aa);
      }
      if (store_bundles.empty() && reduction_bundles.empty()) {
        return loops_changed ? PreservedAnalysis::none()
                             : PreservedAnalysis::all();
      }
      if constexpr (debug_print) {
        fmt::println("Stor {} Red {}", store_bundles.size(),
                     reduction_bundles.size());
      }
    }

    bool modified = false;
    auto run = [&](TVec<SeedBundle> &bundles, bool reduction) {
      for (auto &b : bundles) {
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
        // these live in the temp arena and are used by the plan, so they must
        // not be (re)built inside the window below
        b.block->order.ensure();
        b.block->mem();
        auto stor = utils::TempAlloc<void *>::save();
        auto res = tree_vectorize(ctx, b, reduction);
        if (res != VecResult::Failed) {
          if (res == VecResult::Applied) {
            modified = true;
            b.block->order.invalidate();
          }
          utils::TempAlloc<void *>::restore(stor);
        }
      }
    };
    {
      ZoneScopedN("Run");
      run(store_bundles, false);
      run(reduction_bundles, true);
    }

    if (loops_changed) {
      return PreservedAnalysis::none();
    }
    // the SLP part only changes instructions, never blocks or edges
    return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
  }
};

}  // namespace foptim::optim
