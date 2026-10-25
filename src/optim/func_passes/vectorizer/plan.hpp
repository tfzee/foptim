#pragma once
#include <algorithm>
#include <optional>

#include "config/compiler_config.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/analysis/mem/basic_alias_test.hpp"
#include "optim/analysis/mem/block_mem_access.hpp"
#include "optim/analysis/mem/loop_mem_dep.hpp"
#include "utils/arena.hpp"

namespace foptim::optim::vec {

using Features = conf::Target::Features;

struct Config {
  bool reductions = true;
  bool loops = true;
  // upper bound on the vector factor, 0 = only the target decides
  u32 max_vf = 0;
  // trip count of the cost model when it is not a constant
  u32 assumed_trip = 64;
};

// Results of multi result calls are plain external values, not lanes.
inline bool lane_instr(fir::ValueR v) {
  return v.is_instr() && !v.as_instr().is_multi_result();
}

// Appends the scalars a vector is made of, in lane order. Understood are an
// InsertElement chain on poison that sets every lane, a Concat or a constant
// mask Shuffle of builds, a Broadcast and a constant vector. Inner values need
// a single user so they die with the build.
struct BuildInfo {
  u32 instrs = 0;
  // the InsertElements among them
  u32 inserts = 0;
};
inline bool build_lanes(fir::ValueR v, TVec<fir::ValueR> &out, BuildInfo &info,
                        bool is_root = true, u32 allowed_uses = 1) {
  if (!v.get_type()->is_vec()) {
    return false;
  }
  const u32 n = v.get_type()->as_vec().member_number;
  if (v.is_constant()) {
    if (!v.as_constant()->is_vec()) {
      return false;
    }
    for (const auto &m : v.as_constant()->as_vec().members) {
      out.push_back(fir::ValueR{m});
    }
    return out.size() >= n;
  }
  if (!lane_instr(v) || (!is_root && v.get_n_uses() != allowed_uses)) {
    return false;
  }
  auto instr = v.as_instr();
  if (instr->is(fir::VectorISubType::Concat)) {
    info.instrs++;
    return build_lanes(instr->args[0], out, info, false) &&
           build_lanes(instr->args[1], out, info, false);
  }
  if (instr->is(fir::VectorISubType::Shuffle)) {
    // lane i is a lane of the first or of the second operand
    const auto &mask_v = instr->args[2];
    if (!mask_v.is_constant() || !mask_v.as_constant()->is_vec()) {
      return false;
    }
    const bool same = instr->args[0] == instr->args[1];
    const bool second_unused = instr->args[1].is_poison();
    TVec<fir::ValueR> first;
    TVec<fir::ValueR> second;
    info.instrs++;
    if (!build_lanes(instr->args[0], first, info, false, same ? 2 : 1)) {
      return false;
    }
    if (same) {
      second = first;
    } else if (!second_unused &&
               !build_lanes(instr->args[1], second, info, false)) {
      return false;
    }
    for (const auto &m : mask_v.as_constant()->as_vec().members) {
      const auto idx = m->as_int();
      if (idx < 0) {
        return false;
      }
      if (static_cast<size_t>(idx) < first.size()) {
        out.push_back(first[idx]);
      } else if (static_cast<size_t>(idx) - first.size() < second.size()) {
        out.push_back(second[idx - first.size()]);
      } else {
        return false;
      }
    }
    return true;
  }
  if (instr->is(fir::VectorISubType::Broadcast)) {
    info.instrs++;
    for (u32 i = 0; i < n; i++) {
      out.push_back(instr->args[0]);
    }
    return true;
  }
  if (!instr->is(fir::InstrType::InsertElement)) {
    return false;
  }
  TVec<fir::ValueR> lanes;
  TVec<bool> set;
  lanes.resize(n);
  set.resize(n, false);
  fir::ValueR cur = v;
  bool first = true;
  while (true) {
    if (!lane_instr(cur) ||
        !cur.as_instr()->is(fir::InstrType::InsertElement) ||
        (!first && cur.get_n_uses() != 1)) {
      break;
    }
    auto link = cur.as_instr();
    if (link->get_n_args() != 3 || !link->args[2].is_constant() ||
        !link->args[2].as_constant()->is_int()) {
      return false;
    }
    const auto idx = link->args[2].as_constant()->as_int();
    if (idx < 0 || idx >= n) {
      return false;
    }
    // the latest insert of a lane wins
    if (!set[idx]) {
      set[idx] = true;
      lanes[idx] = link->args[1];
    }
    info.instrs++;
    info.inserts++;
    cur = link->args[0];
    first = false;
  }
  if (!cur.is_poison() || !std::ranges::all_of(set, [](bool b) { return b; })) {
    return false;
  }
  for (auto l : lanes) {
    out.push_back(l);
  }
  return true;
}

// Program order of the instructions of one block. It is invalidated when a
// bundle was applied, since vector code is emitted next to the program last
// lane of a node. ensure must be called outside of a TempAlloc save/restore
// window, the map lives in the temp arena.
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

struct BlockInfo {
  fir::BasicBlock bb;
  BlockOrder order;
  std::optional<BlockMemAccesses> mem_;

  explicit BlockInfo(fir::BasicBlock bb_) : bb(bb_), order(bb_) {}
  // built on first use, so call it outside of a TempAlloc window too
  BlockMemAccesses &mem() {
    if (!mem_) {
      mem_.emplace(bb);
    }
    return *mem_;
  }
  // the block changed, rebuilt by the next call of order.ensure() / mem()
  void invalidate() {
    order.invalidate();
    mem_.reset();
  }
};

enum class LaneSpace : u8 {
  // lane i is instruction i of a seed, the lane count is fixed by the seed
  Bundle,
  // lane i is iteration i of an innermost loop, the vf is chosen when costing
  LoopIter,
};

// Bundle nodes only record the shape, loop plans use it to tell vector values
// from scalar ones.
enum class LaneShape : u8 {
  Uniform,
  Consecutive,
  Varying,
  // loops only, kept as the scalar of lane 0 (lane i is base + i * stride)
  Strided,
  // loops only, loop carried vector such as a reduction accumulator
  Recurrence,
};

// the operation of a reduction loop
enum class RedKind : u8 { Add, Mul, And, Or, Xor, SMin, SMax, UMin, UMax };

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
  // loops: header argument that steps by a constant per iteration
  Induction,
  // loops: header argument that is an accumulator (acc2 = acc op x)
  Reduction,
  // loops: copied as is, computes lane 0
  Scalar,
  // the result is a mask (vector of i1)
  Cmp,
  // loops: block argument inside the body, per lane the value of the edge taken
  Phi,
  // zext (sub 0) or sext (sub 1) of integer lanes to wider integer lanes
  Widen,
  // bundles: replaces a built vector
  BuildRoot,
};

// One widened value of a plan. Nodes refer to each other by index into
// Plan::nodes, children are in reverse operand order. Bundle plans are trees,
// loop plans are in program order with the header arguments first. Only lives
// in the temp arena.
struct PlanNode {
  NodeKind kind = NodeKind::Broadcast;
  LaneShape shape = LaneShape::Varying;
  bool after = false;
  u32 n_lanes = 0;
  // bundles: the widened code is built relative to this instruction
  fir::Instr insert_loc;
  TVec<u32> children;
  // scalar result type of a lane
  fir::TypeR type;
  // compare: type of the compared lanes
  fir::TypeR op_type;
  // sub type of the scalar instruction
  u32 sub = 0;
  // bundles: broadcast source, callee of a call. Loops: the scalar value
  fir::ValueR value;
  fir::Instr aux;
  // constant / gather: scalar value of every lane
  TVec<fir::ValueR> lanes;
  // load: the lanes are gathered from value (the base pointer) plus these
  // element offsets, the node has no children
  TVec<i64> gather_idx;
  // load with gather_idx: full vector loads at these element offsets from
  // value instead of the gather, lane i is lane shuf_mask[i] of the loads
  // concatenated
  TVec<i64> shuf_starts;
  TVec<u32> shuf_mask;
  i64 stride = 0;
  // store of built vectors: scalar instructions it replaces, 0 for plain
  // stores
  u32 scalar_ops = 0;
  // Induction / Reduction / Phi: index of the (block) argument
  u32 arg_id = 0;
  RedKind red = RedKind::Add;
  // loops: index into LoopData::blocks
  u32 block = 0;
};

// Edge leaving block from through successor slot slot of its terminator.
struct LoopEdge {
  u32 from = 0;
  u32 slot = 0;
};

// A block of the loop body. The body is acyclic apart from the back edge, the
// blocks are in reverse post order so every value is defined before it is
// used. Blocks that are not executed in every iteration have a mask, the
// lanes that take the path to the block.
struct LoopBlock {
  fir::BasicBlock bb{fir::BasicBlock::invalid()};
  // edges inside the body that enter the block
  TVec<LoopEdge> in_edges;
  // block that executes in exactly the same lanes (this one post dominates its
  // immediate dominator), ~0U if none
  u32 same_mask_as = ~0U;
  // executes in every lane
  bool always = false;
  // lane width of the compares the mask is made of, 0 if always
  u32 mask_bits = 0;
};

struct LoopData {
  TMap<fir::ValueR, u32> node_of;
  fir::BasicBlock header{fir::BasicBlock::invalid()};
  // block with the exit test and the back edge
  fir::BasicBlock latch{fir::BasicBlock::invalid()};
  // header first, latch last
  TVec<LoopBlock> blocks;
  u32 mask_ops = 0;
  u32 back_edge = 0;
  u32 exit_edge = 0;
  // induction variable with step 1 that counts the vector iterations
  u32 counter = 0;
  // number of header executions
  SCEVRef trip = InvalidSCEV;
  std::optional<i128> const_trip;
  // narrowest and widest lane of the vector values, they are the same for most
  // loops, otherwise the vector values have several registers (parts)
  u32 min_bits = 0;
  u32 max_bits = 0;
  // lanes of one register of every node at the chosen vf, 0 for scalars
  TVec<u32> part_lanes;
  u32 n_vector_ops = 0;
  u32 n_scalar_ops = 0;
  u32 n_reductions = 0;
  TVec<LoopMemDep::Access> accesses;
  // instructions of the body without the terminator
  u32 scalar_cost = 0;
};

// The unit of selection. Planning is read only, only apply mutates the IR.
// nodes[0] is the root of a bundle plan.
struct Plan {
  LaneSpace space = LaneSpace::Bundle;
  TVec<PlanNode> nodes;
  // LoopIter only
  LoopData loop;
  ScalarEvo *evo = nullptr;
  // bundles: the lanes of the root
  u32 vf = 0;
  // scalar cost minus vector cost, higher is better. A bundle plan is per
  // execution of its block, a loop plan covers all iterations.
  i64 benefit = 0;
};

enum class SeedKind : u8 { Store, Reduction, Build, Loop };

struct SeedInstrData {
  fir::Instr instr;
  // a store of a built vector covers several lanes
  u32 lanes = 1;
  // instructions of the build that die with the store
  u32 build_instrs = 0;
};
struct Seed {
  fir::ValueR base;
  fir::TypeR type;
  TVec<SeedInstrData> data;
  // the header for a loop
  BlockInfo *block = nullptr;
  SeedKind kind = SeedKind::Store;
  LoopInfo *loop = nullptr;
  // reductions: the lanes may be taken in any order
  bool assoc = false;
  // builds: the scalars of the vector base
  TVec<fir::ValueR> lane_values;

  [[nodiscard]] u32 n_lanes() const {
    u32 n = 0;
    for (const auto &d : data) {
      n += d.lanes;
    }
    return n;
  }
};

struct SeedRegion {
  BlockInfo *info = nullptr;
  LoopInfo *loop = nullptr;
  AliasAnalyis &aa;
  const conf::CompConf &conf;
  const Config &config;
};

struct PlanEnv {
  fir::Context &ctx;
  fir::Function &func;
  // only valid while planning loops
  CFG *cfg;
  Dominators *dom;
  AliasAnalyis &aa;
  const Config &config;
};

class SeedProvider {
 public:
  TVec<Seed> seeds;
  virtual void scan(SeedRegion &region) = 0;
  virtual void finalize(const conf::CompConf &conf) = 0;
  virtual bool plan(PlanEnv &env, Seed &seed, Plan &plan) = 0;
  virtual ~SeedProvider() = default;
};

}  // namespace foptim::optim::vec
