#pragma once
#include <span>

#include "optim/func_passes/vectorizer/plan.hpp"

namespace foptim::optim::vec {

// what a node is initialized with: the seed block and where the node's parent
// is emitted (invalid for the root)
struct InitCtx {
  BlockInfo &info;
  fir::Instr parent_loc;
};

struct BundleCtx {
  BlockInfo &info;
  AliasAnalyis &aa;
  const Features &features;
  // out: one lane is the first operand of the base lane (x + (x + y))
  bool potential_neutral = false;
};

struct LoopCtx {
  Plan &plan;
  ScalarEvo &evo;
};

// One recipe per widenable instruction type.
// match_* decide whether the instruction(s) of the lanes can be this node
// (bundles: do all lanes agree and may the memory accesses move, loops: does
// the access step by the element size), legal and cost are shared by both
// lane spaces.
struct Recipe {
  fir::InstrType type;
  NodeKind kind;
  const char *name;
  bool (*match_bundle)(const TVec<fir::ValueR> &, BundleCtx &);
  // null if the instruction cannot be widened over loop iterations
  bool (*match_loop)(LoopCtx &, fir::Instr, PlanNode &);
  void (*init)(PlanNode &, const TVec<fir::ValueR> &, const InitCtx &);
  size_t (*n_args)(const TVec<fir::ValueR> &);
  bool (*legal)(const PlanNode &, LaneSpace, u32 vf, const Features &);
  // one vector instruction of vf lanes
  i64 (*cost)(const PlanNode &, u32 vf, const Features &);
};

const Recipe *find_recipe(fir::InstrType type);
const Recipe *recipe_of(NodeKind kind);

bool node_legal(const PlanNode &n, LaneSpace space, u32 vf,
                const Features &features);
i64 node_vec_cost(const PlanNode &n, u32 vf, const Features &features);
i64 node_scalar_cost(const PlanNode &n);
i64 bundle_benefit(const Plan &plan, u32 id, const Features &features);

// operands in operand order, the builder is positioned by the caller
fir::ValueR emit_node(fir::Context &ctx, fir::Builder &bb, const PlanNode &n,
                      u32 vf, std::span<const fir::ValueR> ops);

void init_back(PlanNode &n, const TVec<fir::ValueR> &values, const InitCtx &c);
bool broadcast_match(const TVec<fir::ValueR> &values);
void broadcast_init(PlanNode &n, const TVec<fir::ValueR> &values);
bool constant_match(const TVec<fir::ValueR> &values);
// the backend has register types for these vectors only
bool vector_type_ok(fir::TypeR elem, u32 lanes, const Features &features);
bool gather_legal(const TVec<fir::ValueR> &values, const Features &features);
void gather_init(PlanNode &n, const TVec<fir::ValueR> &values,
                 const InitCtx &c);
// all lanes zero
fir::ValueR zero_vector(fir::Context &ctx, fir::TypeR elem, u32 lanes);

// The reduce operation for the chain rooted in root, nullopt if the lanes have
// to be extracted and combined as scalars.
std::optional<fir::ReduceInstrSubType> reduce_op_for(fir::Instr root, u32 lanes,
                                           const Features &features);
// The operations that are lowered by halving the vector.
std::optional<fir::ReduceInstrSubType> halving_op_for(fir::Instr root, u32 lanes,
                                           const Features &features);

void node_dump(const Plan &plan, u32 id, u32 indent = 0);

// Lane width of the vector compares a mask is made of, 0 if the value is not a
// compare or a logic operation of compares of one width.
u32 mask_bits_of(fir::ValueR v);
// masks (and so compares and selects) exist for 8 to 64 bit lanes up to 256 bit
bool mask_width_ok(u32 bits, u32 vf);

bool elem_type_ok(fir::TypeR ty);
bool is_vector_shape(LaneShape s);
u32 shape_node(const LoopCtx &c, fir::ValueR v);
LaneShape shape_of(const LoopCtx &c, fir::ValueR v);

}  // namespace foptim::optim::vec
