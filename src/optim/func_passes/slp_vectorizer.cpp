#include "slp_vectorizer.hpp"

#include <fmt/base.h>
#include <fmt/core.h>

#include <algorithm>
#include <llvm/IR/Value.h>

#include "config/compiler_config.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/value.hpp"
#include "optim/helper/WFVector.hpp"
#include "utils/arena.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

namespace {
using Features = conf::Target::Features;
using PlanNode = SLPVectorizer::PlanNode;
using NodeKind = SLPVectorizer::NodeKind;
using LaneShape = SLPVectorizer::LaneShape;
using Plan = SLPVectorizer::Plan;
using SeedBundle = SLPVectorizer::SeedBundle;
constexpr u32 NoNode = std::numeric_limits<u32>::max();

// the three lane variants most node kinds pick their instruction from: the
// last lane if it is an instruction, otherwise the first one
fir::Instr pick_instr(const TVec<fir::ValueR> &values) {
  return values.back().is_instr() ? values.back().as_instr()
                                  : values.front().as_instr();
}
void init_flex(PlanNode &n, const TVec<fir::ValueR> &values) {
  n.n_lanes = values.size();
  auto pick = pick_instr(values);
  if (values.back().is_bb_arg()) {
    n.insert_loc = values.back().as_bb_arg()->get_parent()->instructions[0];
  } else {
    n.insert_loc = pick;
  }
  n.type = pick->get_type();
  n.sub = pick->subtype;
}
void init_back(PlanNode &n, const TVec<fir::ValueR> &values) {
  n.n_lanes = values.size();
  n.insert_loc = values.back().as_instr();
  n.type = n.insert_loc->get_type();
  n.sub = n.insert_loc->subtype;
}
bool same_kind_instrs(const TVec<fir::ValueR> &values, fir::Instr base_v) {
  for (auto i_v : values) {
    if (!i_v.is_instr()) {
      return false;
    }
    auto i = i_v.as_instr();
    if (i->instr_type != base_v->instr_type || i->subtype != base_v->subtype) {
      return false;
    }
  }
  return true;
}

// ---- Broadcast (not a recipe, any operand where all lanes are the same) ----
bool broadcast_match(const TVec<fir::ValueR> &values) {
  auto base_v = values.back();
  if (base_v.is_constant()) {
    return false;
  }
  auto ty = base_v.get_type();
  if (!ty->is_int() && !ty->is_ptr() && !ty->is_float()) {
    return false;
  }
  for (auto v : values) {
    if (v != base_v) {
      return false;
    }
  }
  return true;
}
void broadcast_init(PlanNode &n, const TVec<fir::ValueR> &values) {
  n.n_lanes = values.size();
  if (values.back().is_bb_arg()) {
    n.insert_loc = values.back().as_bb_arg()->_parent->instructions[0];
    n.after = false;
  } else {
    n.insert_loc = values.back().as_instr();
    n.after = true;
  }
  n.value = values.back();
  n.shape = LaneShape::Uniform;
}

// ---- recipes: match = do the lanes agree, legal = can the target do it ----
struct RecipeCtx {
  const TVec<SeedBundle> &load_bundles;
  const Features &features;
  // out: one lane is the first operand of the base lane (x + (x + y))
  bool potential_neutral = false;
};
struct Recipe {
  fir::InstrType type;
  NodeKind kind;
  const char *name;
  bool (*match)(const TVec<fir::ValueR> &, RecipeCtx &);
  bool (*legal)(const TVec<fir::ValueR> &, RecipeCtx &);
  void (*init)(PlanNode &, const TVec<fir::ValueR> &);
  size_t (*n_args)(const TVec<fir::ValueR> &);
};

bool always_legal(const TVec<fir::ValueR> & /*values*/, RecipeCtx & /*ctx*/) {
  return true;
}
size_t n_args_1(const TVec<fir::ValueR> & /*values*/) { return 1; }
size_t n_args_instr(const TVec<fir::ValueR> &values) {
  return values[0].as_instr()->get_n_args();
}

bool match_same_instr(const TVec<fir::ValueR> &values, RecipeCtx & /*ctx*/) {
  return same_kind_instrs(values, pick_instr(values));
}
bool match_same_instr_back(const TVec<fir::ValueR> &values,
                           RecipeCtx & /*ctx*/) {
  return same_kind_instrs(values, values.back().as_instr());
}

bool store_match(const TVec<fir::ValueR> & /*values*/, RecipeCtx & /*ctx*/) {
  // assuming it works out ?
  return true;
}
void store_init(PlanNode &n, const TVec<fir::ValueR> &values) {
  init_back(n, values);
  n.aux = values[0].as_instr();
  n.shape = LaneShape::Consecutive;
}

bool binary_match(const TVec<fir::ValueR> &values, RecipeCtx &ctx) {
  auto base_v = pick_instr(values);
  for (auto i_v : values) {
    if (i_v == base_v->args[0]) {
      ctx.potential_neutral = true;
      continue;
    }
    if (!i_v.is_instr()) {
      return false;
    }
    auto i = i_v.as_instr();
    if (i->instr_type != base_v->instr_type || i->subtype != base_v->subtype) {
      return false;
    }
  }
  if (base_v->args[1].get_type()->get_bitwidth() !=
      base_v->args[0].get_type()->get_bitwidth()) {
    return false;
  }
  switch (static_cast<fir::BinaryInstrSubType>(base_v->subtype)) {
  case fir::BinaryInstrSubType::FloatAdd:
  case fir::BinaryInstrSubType::IntAdd:
  case fir::BinaryInstrSubType::PtrAdd:
  case fir::BinaryInstrSubType::Shl:
  case fir::BinaryInstrSubType::IntSub:
  case fir::BinaryInstrSubType::Xor:
  case fir::BinaryInstrSubType::Or:
  case fir::BinaryInstrSubType::Shr:
  case fir::BinaryInstrSubType::AShr:
    return true;
  case fir::BinaryInstrSubType::FloatSub:
  case fir::BinaryInstrSubType::FloatMul:
  case fir::BinaryInstrSubType::IntMul:
  case fir::BinaryInstrSubType::And:
  case fir::BinaryInstrSubType::FloatDiv:
    // we cant handle neutral elemtns for everything
    // TODO. this causes issues + technically there is a neutral element for
    // div
    return !ctx.potential_neutral;
  case fir::BinaryInstrSubType::IntSRem:
  case fir::BinaryInstrSubType::IntURem:
  case fir::BinaryInstrSubType::IntSDiv:
  case fir::BinaryInstrSubType::IntUDiv:
    return false;
  case fir::BinaryInstrSubType::INVALID:
    fmt::println("{}", base_v);
    TODO("impl");
    return false;
  }
}

bool load_match(const TVec<fir::ValueR> &values, RecipeCtx &ctx) {
  for (auto v : values) {
    if (!v.is_instr()) {
      return false;
    }
  }
  auto base_v = values.back().as_instr();
  if (base_v->get_type()->is_vec()) {
    // already vec
    return false;
  }
  for (auto i_v : values) {
    auto i = i_v.as_instr();
    if (i->instr_type != base_v->instr_type || i->subtype != base_v->subtype) {
      return false;
    }
  }
  for (const auto &load_bundle : ctx.load_bundles) {
    bool found_all = true;
    for (const auto &value : values) {
      bool inside_bundle = false;
      for (const auto &bundle_vals : load_bundle.data) {
        if (bundle_vals.instr == value.as_instr()) {
          inside_bundle = true;
          break;
        }
      }
      if (!inside_bundle) {
        found_all = false;
        break;
      }
    }
    if (found_all) {
      return true;
    }
  }
  return false;
}
bool load_legal(const TVec<fir::ValueR> &values, RecipeCtx &ctx) {
  auto base_v = values.back().as_instr();
  // TODO dont know how to grather i8 and stuff efficiently
  if (base_v->get_type()->get_bitwidth() <= 16) {
    return false;
  }
  // TODO impl vec for bigger sizes
  return !(values.size() > 8 ||
           (!ctx.features.avx512f && values.size() > 4));
}
void load_init(PlanNode &n, const TVec<fir::ValueR> &values) {
  init_back(n, values);
  n.aux = values[0].as_instr();
  n.shape = LaneShape::Consecutive;
}

bool unary_match(const TVec<fir::ValueR> &values, RecipeCtx &ctx) {
  if (!match_same_instr_back(values, ctx)) {
    return false;
  }
  switch (static_cast<fir::UnaryInstrSubType>(
      values.back().as_instr()->subtype)) {
  case fir::UnaryInstrSubType::INVALID:
    return false;
  case fir::UnaryInstrSubType::IntNeg:
    fmt::println("{}", values.back().as_instr());
    TODO("impl it?");
  case fir::UnaryInstrSubType::Not:
  case fir::UnaryInstrSubType::FloatNeg:
  case fir::UnaryInstrSubType::FloatSqrt:
    break;
  }
  return true;
}

bool extract_match(const TVec<fir::ValueR> &values, RecipeCtx & /*ctx*/) {
  auto base_v = values.back().as_instr();
  auto exp_off = 0;
  auto input_val = base_v->args[0];
  if (!input_val.get_type()->is_vec()) {
    return false;
  }
  for (auto i_v : values) {
    if (!i_v.is_instr()) {
      return false;
    }
    auto i = i_v.as_instr();
    if (i->instr_type != base_v->instr_type || i->subtype != base_v->subtype) {
      return false;
    }
    if (!i->args[1].is_const_int(exp_off) || i->args[0] != input_val) {
      return false;
    }
    exp_off++;
  }
  return true;
}
size_t n_args_0(const TVec<fir::ValueR> & /*values*/) { return 0; }

bool call_match(const TVec<fir::ValueR> &values, RecipeCtx & /*ctx*/) {
  auto base_v = pick_instr(values);
  if (!base_v->args[0].is_constant_func()) {
    return false;
  }
  auto called_func = base_v->args[0].as_constant()->as_func();
  if (!called_func->attribs.maybe_can_wfvec) {
    return false;
  }
  for (auto i_v : values) {
    if (!i_v.is_instr()) {
      return false;
    }
    auto i = i_v.as_instr();
    if (!i->args[0].is_constant_func()) {
      return false;
    }
    if (i->args[0] != base_v->args[0]) {
      return false;
    }
    if (i->instr_type != base_v->instr_type || i->subtype != base_v->subtype) {
      return false;
    }
  }
  return true;
}
void call_init(PlanNode &n, const TVec<fir::ValueR> &values) {
  init_back(n, values);
  n.value = values.back().as_instr()->args[0];
}

bool conversion_legal(const TVec<fir::ValueR> &values, RecipeCtx & /*ctx*/) {
  // the backend only has scalar fp <-> int / ext / trunc conversions
  switch (static_cast<fir::ConversionSubType>(pick_instr(values)->subtype)) {
  case fir::ConversionSubType::BitCast:
  case fir::ConversionSubType::PtrToInt:
  case fir::ConversionSubType::IntToPtr:
    return true;
  default:
    return false;
  }
}

bool intrin_legal(const TVec<fir::ValueR> &values, RecipeCtx & /*ctx*/) {
  switch (static_cast<fir::IntrinsicSubType>(values.back().as_instr()->subtype)) {
  case fir::IntrinsicSubType::INVALID:
  case fir::IntrinsicSubType::VA_start:
  case fir::IntrinsicSubType::VA_end:
  case fir::IntrinsicSubType::IsConstant:
  case fir::IntrinsicSubType::Memset:
  case fir::IntrinsicSubType::Memcpy:
    return false;
  case fir::IntrinsicSubType::PopCnt:
  case fir::IntrinsicSubType::CTLZ:
  case fir::IntrinsicSubType::CTTZ:
  case fir::IntrinsicSubType::FMin:
  case fir::IntrinsicSubType::FMax:
  case fir::IntrinsicSubType::FMinimum:
  case fir::IntrinsicSubType::FMaximum:
  case fir::IntrinsicSubType::UMin:
  case fir::IntrinsicSubType::UMax:
  case fir::IntrinsicSubType::SMin:
  case fir::IntrinsicSubType::SMax:
  case fir::IntrinsicSubType::Abs:
  case fir::IntrinsicSubType::FAbs:
  case fir::IntrinsicSubType::FRound:
  case fir::IntrinsicSubType::FCeil:
  case fir::IntrinsicSubType::FFloor:
  case fir::IntrinsicSubType::FTrunc:
    return true;
  }
}

bool constant_match(const TVec<fir::ValueR> &values) {
  auto base_v = values.back().as_constant();
  for (auto i_v : values) {
    if (!i_v.is_constant()) {
      return false;
    }
    // TODO: this wont work cause of tracking the value uses will break
    if (i_v.as_constant()->is_func() || i_v.as_constant()->is_global()) {
      return false;
    }
    if (i_v.get_type() != base_v->get_type()) {
      return false;
    }
  }
  return true;
}

// Operands that no recipe matches could be built from scalars (a Gather
// node). The node, its cost and the planner hook exist, but emission is not
// wired up: the backend aborts on `insertelement` into integer vectors
// (known-issues.md), so nothing may claim a gather yet.
bool gather_legal(const TVec<fir::ValueR> & /*values*/,
                  const Features & /*features*/) {
  return false;
}

const Recipe Recipes[] = {
    {fir::InstrType::StoreInstr, NodeKind::Store, "store", store_match,
     always_legal, store_init, [](const TVec<fir::ValueR> &) -> size_t {
       return 2;
     }},
    {fir::InstrType::BinaryInstr, NodeKind::Binary, "binary", binary_match,
     always_legal, init_flex,
     [](const TVec<fir::ValueR> &) -> size_t { return 2; }},
    {fir::InstrType::LoadInstr, NodeKind::Load, "load", load_match, load_legal,
     load_init, n_args_1},
    {fir::InstrType::Intrinsic, NodeKind::Intrin, "intrinsic",
     match_same_instr_back, intrin_legal, init_back, n_args_instr},
    {fir::InstrType::ITrunc, NodeKind::ITrunc, "itrunc", match_same_instr_back,
     always_legal, init_back, n_args_1},
    {fir::InstrType::ZExt, NodeKind::Zext, "zext", match_same_instr,
     always_legal, init_flex, n_args_1},
    {fir::InstrType::UnaryInstr, NodeKind::Unary, "unary", unary_match,
     always_legal, init_back, n_args_1},
    {fir::InstrType::ExtractValue, NodeKind::Extract, "extract", extract_match,
     always_legal, init_back, n_args_0},
    {fir::InstrType::CallInstr, NodeKind::Call, "call", call_match,
     always_legal, call_init, n_args_instr},
    {fir::InstrType::Conversion, NodeKind::Conversion, "conversion",
     match_same_instr, conversion_legal, init_flex, n_args_1},
    {fir::InstrType::SelectInstr, NodeKind::Select, "select", match_same_instr,
     always_legal, init_flex,
     [](const TVec<fir::ValueR> &) -> size_t { return 3; }},
};

const Recipe *find_recipe(fir::InstrType type) {
  for (const auto &r : Recipes) {
    if (r.type == type) {
      return &r;
    }
  }
  return nullptr;
}

// ---- cost, higher = better ----
i64 node_cost(const Plan &plan, u32 id, const Features &features) {
  const auto &n = plan.nodes[id];
  auto child = [&](size_t i) { return node_cost(plan, n.children.at(i), features); };
  auto lanes = static_cast<i64>(n.n_lanes);
  switch (n.kind) {
  case NodeKind::Broadcast:
    return -1;
  case NodeKind::Gather:
    return -lanes;
  case NodeKind::HorizRed:
    return child(0) - (lanes / 2);
  case NodeKind::Binary: {
    auto cost = child(0) + child(1) + lanes;
    if (static_cast<fir::BinaryInstrSubType>(n.sub) ==
            fir::BinaryInstrSubType::IntMul &&
        !features.avx512dq && n.type->is_int()) {
      cost -= 8;
    }
    return cost;
  }
  case NodeKind::Zext:
  case NodeKind::ITrunc:
  case NodeKind::Load:
    return child(0) + lanes;
  case NodeKind::Call: {
    i64 cost = 0;
    for (size_t i = 0; i < n.children.size(); i++) {
      cost += child(i);
    }
    return cost;
  }
  case NodeKind::Unary:
  case NodeKind::Store:
    return child(0) + (lanes * 2);
  case NodeKind::Extract:
    return -2;
  case NodeKind::Intrin:
    if (n.children.size() == 2) {
      return child(0) + child(1) + (lanes * 2);
    }
    return child(0) + lanes;
  case NodeKind::Constant:
    return lanes;
  case NodeKind::Conversion:
  case NodeKind::Select:
    // TODO switch on type of cast
    return 1;
  }
  UNREACH();
}

// ---- emit ----
fir::ValueR node_emit(fir::Context &ctx, const Plan &plan, u32 id,
                      const SeedBundle &bundle) {
  const auto &n = plan.nodes[id];
  auto emit_child = [&](size_t i) {
    return node_emit(ctx, plan, n.children.at(i), bundle);
  };
  switch (n.kind) {
  case NodeKind::Broadcast: {
    fir::Builder bb{n.insert_loc};
    if (n.after) {
      bb.after(n.insert_loc);
    }
    return bb.build_vbroadcast(n.value,
                               ctx->get_vec_type(n.value.get_type(), n.n_lanes));
  }
  case NodeKind::Gather:
    TODO("gather emission is not implemented, see gather_legal");
  case NodeKind::HorizRed: {
    auto orig_instr = bundle.base.as_instr();
    auto subtype = orig_instr->subtype;
    ASSERT(n.children.size() == 1);
    bool isProd =
        subtype == static_cast<u32>(fir::BinaryInstrSubType::FloatMul) ||
        subtype == static_cast<u32>(fir::BinaryInstrSubType::IntMul);
    bool isSum =
        subtype == static_cast<u32>(fir::BinaryInstrSubType::FloatAdd) ||
        subtype == static_cast<u32>(fir::BinaryInstrSubType::IntAdd);
    ASSERT(isProd || isSum);
    auto vec = emit_child(0);
    fir::Builder bb{orig_instr};
    bb.after(orig_instr);
    auto new_v =
        bb.build_vector_op(vec, orig_instr->get_type(),
                           isProd ? fir::VectorISubType::HorizontalMul
                                  : fir::VectorISubType::HorizontalAdd);
    // the scalar chain is erased by the driver once everything is emitted
    orig_instr->replace_all_uses(new_v);
    return new_v;
  }
  case NodeKind::Binary: {
    auto bv = emit_child(0);
    auto av = emit_child(1);
    fir::Builder bb{n.insert_loc};
    return bb.build_binary_op(av, bv,
                              static_cast<fir::BinaryInstrSubType>(n.sub));
  }
  case NodeKind::Zext: {
    auto val = emit_child(0);
    fir::Builder bb{n.insert_loc};
    return bb.build_zext(val, ctx->get_vec_type(n.type, n.n_lanes));
  }
  case NodeKind::ITrunc: {
    auto val = emit_child(0);
    fir::Builder bb{n.insert_loc};
    return bb.build_itrunc(val, ctx->get_vec_type(n.type, n.n_lanes));
  }
  case NodeKind::Call: {
    // children are in reverse operand order, the last one is the callee
    TVec<fir::ValueR> args;
    args.resize(n.children.size() - 1);
    for (size_t i = 0; i < n.children.size() - 1; i++) {
      args[n.children.size() - 2 - i] = emit_child(i);
    }
    fir::Builder bb{n.insert_loc};
    auto *funcy = n.value.as_constant()->as_func().func;
    auto new_func = whole_function_vectorize(*funcy, n.n_lanes);
    ASSERT(new_func.has_value())
    auto ftype = new_func.value()->func_ty;
    return bb.build_call(fir::ValueR{ctx->get_constant_value(new_func.value())},
                         ftype, ftype->as_func().return_type, args);
  }
  case NodeKind::Unary: {
    auto val = emit_child(0);
    fir::Builder bb{n.insert_loc};
    return bb.build_unary_op(val, static_cast<fir::UnaryInstrSubType>(n.sub));
  }
  case NodeKind::Extract:
    return n.insert_loc->args[0];
  case NodeKind::Store: {
    auto val = emit_child(0);
    fir::Builder bb{n.insert_loc};
    bb.after(n.insert_loc);
    // TODO: assuming continious stores
    // the scalar stores are erased by the driver once everything is emitted
    return bb.build_store(n.aux->args[0], val, false, false);
  }
  case NodeKind::Intrin: {
    // TODO: assuming continious stores
    auto val = emit_child(0);
    fir::Builder bb{n.insert_loc};
    switch (static_cast<fir::IntrinsicSubType>(n.sub)) {
    default:
      TODO("UNREACH");
    case fir::IntrinsicSubType::CTLZ:
    case fir::IntrinsicSubType::UMin:
    case fir::IntrinsicSubType::UMax:
    case fir::IntrinsicSubType::SMin:
    case fir::IntrinsicSubType::SMax:
      TODO("impl");
    case fir::IntrinsicSubType::FMin:
      return bb.build_intrinsic(val, emit_child(1),
                                fir::IntrinsicSubType::FMin);
    case fir::IntrinsicSubType::FMax:
      return bb.build_intrinsic(val, emit_child(1),
                                fir::IntrinsicSubType::FMax);
    case fir::IntrinsicSubType::FRound:
      return bb.build_intrinsic(val, fir::IntrinsicSubType::FRound);
    case fir::IntrinsicSubType::FCeil:
      return bb.build_intrinsic(val, fir::IntrinsicSubType::FCeil);
    case fir::IntrinsicSubType::FFloor:
      return bb.build_intrinsic(val, fir::IntrinsicSubType::FFloor);
    case fir::IntrinsicSubType::Abs:
      return bb.build_abs(val);
    case fir::IntrinsicSubType::FAbs:
      return bb.build_fabs(val);
    }
  }
  case NodeKind::Constant: {
    auto n_elems = n.lanes.size();
    IRVec<fir::ConstantValueR> args;
    for (size_t i = 0; i < n_elems; i++) {
      args.push_back(n.lanes[i].as_constant());
    }
    bool all_equal = true;
    for (size_t i = 1; i < args.size(); i++) {
      if (args[i] != args[0]) {
        all_equal = false;
        break;
      }
    }
    auto vec_ty = ctx->get_vec_type(args.at(0)->get_type(), n_elems);
    if (all_equal) {
      fir::Builder b{n.insert_loc};
      return fir::ValueR{b.build_vbroadcast(fir::ValueR{args[0]}, vec_ty)};
    }
    return fir::ValueR{ctx->get_constant_value(std::move(args), vec_ty)};
  }
  case NodeKind::Conversion: {
    auto av = emit_child(0);
    fir::Builder bb{n.insert_loc};
    return bb.build_conversion_op(av, ctx->get_vec_type(n.type, n.n_lanes),
                                  static_cast<fir::ConversionSubType>(n.sub));
  }
  case NodeKind::Select: {
    // children are in reverse operand order: b, a, cond
    auto bv = emit_child(0);
    auto av = emit_child(1);
    auto cond = emit_child(2);
    fir::Builder bb{n.insert_loc};
    return bb.build_select(ctx->get_vec_type(n.type, n.n_lanes), cond, av, bv);
  }
  case NodeKind::Load: {
    auto val = emit_child(0);
    fir::Builder bb{n.insert_loc};
    bb.after(n.insert_loc);
    if (n.insert_loc->get_type()->is_vec()) {
      auto vec_loads = n.insert_loc->get_type()->as_vec();
      auto vec_ty = ctx->get_vec_type(
          vec_loads.type, vec_loads.bitwidth * vec_loads.member_number,
          n.n_lanes);
      return bb.build_load(vec_ty, val, false, false);
    }
    auto vec_ty = ctx->get_vec_type(n.insert_loc->get_type(), n.n_lanes);
    return bb.build_load(vec_ty, val, false, false);
  }
  }
  UNREACH();
}

void node_dump(const Plan &plan, u32 id, u32 indent = 0) {
  static constexpr const char *names[] = {
      "broadcast", "gather", "horiz_red", "binary",  "zext",   "itrunc",
      "call",      "unary",  "extract",   "store",   "intrin", "constant",
      "conversion", "select", "load"};
  static constexpr const char *shapes[] = {"uniform", "consecutive", "varying"};
  const auto &n = plan.nodes[id];
  fmt::println("{:{}}{}<{}> x{}", "", indent * 2,
               names[static_cast<u32>(n.kind)],
               shapes[static_cast<u32>(n.shape)], n.n_lanes);
  for (auto c : n.children) {
    node_dump(plan, c, indent + 1);
  }
}
} // namespace

SLPVectorizer::VecResult
SLPVectorizer::tree_vectorize(fir::Context &ctx, SeedBundle &b,
                              const TVec<SeedBundle> &load_bundles,
                              bool reduction) {
  const auto &features = ctx.config->target.features;
  auto &stats = utils::StatCollector::get();
  // planning is read only (it may only intern constants), the IR is only
  // mutated by `node_emit` below
  Plan plan;
  if (!build_plan(ctx, features, b, load_bundles, reduction, plan)) {
    stats.addi(1, "SLPRejectedNoPlan", utils::StatCollector::StatFOptim);
    return VecResult::Failed;
  }
  if (plan.nodes.empty()) {
    return VecResult::NotWorth;
  }
  auto tree_cost = node_cost(plan, 0, features);
  if (tree_cost <= 0) {
    if constexpr (debug_print) {
      fmt::println("Failed vectorize -> not worth, cost {}", tree_cost);
      node_dump(plan, 0);
    }
    stats.addi(1, "SLPRejectedCost", utils::StatCollector::StatFOptim);
    return VecResult::NotWorth;
  }
  // emit first, erase afterwards: later nodes may still read the scalars
  node_emit(ctx, plan, 0, b);
  if (reduction) {
    b.base.as_instr().destroy();
  } else {
    for (auto &d : b.data) {
      d.instr.destroy();
    }
  }
  stats.addi(1, "SLPVectorized", utils::StatCollector::StatFOptim);
  return VecResult::Applied;
}

bool SLPVectorizer::build_plan(fir::Context &ctx, const Features &features,
                               SeedBundle &b,
                               const TVec<SeedBundle> &load_bundles,
                               bool reduction, Plan &plan) {
  auto add_node = [&](u32 parent, PlanNode node) {
    plan.nodes.push_back(std::move(node));
    auto id = static_cast<u32>(plan.nodes.size() - 1);
    if (parent != NoNode) {
      plan.nodes[parent].children.push_back(id);
    }
    return id;
  };

  u32 root_parent = NoNode;
  if (reduction) {
    ASSERT(b.data.size() % 2 == 0);
    PlanNode red;
    red.kind = NodeKind::HorizRed;
    TVec<fir::ValueR> base{b.base};
    init_back(red, base);
    root_parent = add_node(NoNode, std::move(red));
  }
  TVec<std::pair<u32, TVec<fir::ValueR>>> worklist;
  {
    TVec<fir::ValueR> v;
    for (auto &va : b.data) {
      v.emplace_back(va.instr);
    }
    worklist.emplace_back(root_parent, std::move(v));
  }

  while (!worklist.empty()) {
    auto [parent, curr] = std::move(worklist.back());
    worklist.pop_back();
    auto test_i = curr.back();

    // if they are all the same it can just be a broadcast
    if (broadcast_match(curr)) {
      PlanNode n;
      n.kind = NodeKind::Broadcast;
      broadcast_init(n, curr);
      ASSERT(parent != NoNode);
      add_node(parent, std::move(n));
      continue;
    }
    if (!test_i.is_instr() && curr.front().is_instr()) {
      test_i = curr.front();
    }

    if (test_i.is_instr()) {
      const Recipe *recipe = find_recipe(test_i.as_instr()->instr_type);
      RecipeCtx rctx{.load_bundles = load_bundles, .features = features};
      if (recipe == nullptr || !recipe->match(curr, rctx) ||
          !recipe->legal(curr, rctx)) {
        if (recipe == nullptr && gather_legal(curr, features)) {
          // see gather_legal
          PlanNode n;
          n.kind = NodeKind::Gather;
          n.n_lanes = curr.size();
          n.lanes = curr;
          add_node(parent, std::move(n));
          continue;
        }
        if constexpr (debug_print) {
          fmt::println("Failed tree vectorize at {} like {}",
                       recipe != nullptr ? recipe->name
                                         : "unsupported instruction",
                       curr.back());
        }
        return false;
      }
      PlanNode n;
      n.kind = recipe->kind;
      recipe->init(n, curr);
      auto n_args = recipe->n_args(curr);
      if (recipe->kind == NodeKind::Store) {
        ASSERT(parent == NoNode);
      } else {
        ASSERT(parent != NoNode);
      }
      auto bin_op = static_cast<fir::BinaryInstrSubType>(n.sub);
      auto result = add_node(parent, std::move(n));

      if (recipe->kind == NodeKind::Binary) {
        f64 neutral_float = 0.F;
        i128 neutral_int = 0;
        if (rctx.potential_neutral) {
          switch (bin_op) {
          case fir::BinaryInstrSubType::FloatAdd:
          case fir::BinaryInstrSubType::IntAdd:
          case fir::BinaryInstrSubType::PtrAdd:
          case fir::BinaryInstrSubType::Shl:
          case fir::BinaryInstrSubType::IntSub:
          case fir::BinaryInstrSubType::Xor:
          case fir::BinaryInstrSubType::Or:
            neutral_float = 0.F;
            neutral_int = 0;
            break;
          case fir::BinaryInstrSubType::IntMul:
            neutral_float = 1.F;
            neutral_int = 1;
            break;
          case fir::BinaryInstrSubType::And:
            neutral_float = std::bit_cast<f64>(~static_cast<u64>(0));
            neutral_int = ~static_cast<i128>(0);
            break;
          case fir::BinaryInstrSubType::INVALID:
          case fir::BinaryInstrSubType::IntSRem:
          case fir::BinaryInstrSubType::IntURem:
          case fir::BinaryInstrSubType::IntSDiv:
          case fir::BinaryInstrSubType::IntUDiv:
          case fir::BinaryInstrSubType::Shr:
          case fir::BinaryInstrSubType::AShr:
          case fir::BinaryInstrSubType::FloatSub:
          case fir::BinaryInstrSubType::FloatMul:
          case fir::BinaryInstrSubType::FloatDiv:
            UNREACH();
          }
        }
        auto arg_0 = test_i.as_instr()->args[0];
        for (size_t arg_id = 0; arg_id < n_args; arg_id++) {
          TVec<fir::ValueR> data;
          for (auto c : curr) {
            if (c == arg_0 && arg_id == 1 && test_i != c) {
              auto t = test_i.as_instr()->args[1].get_type();
              if (t->is_float()) {
                // TODO THis is only correct ofr addition
                data.emplace_back(ctx->get_constant_value(neutral_float, t));
              } else {
                data.emplace_back(ctx->get_constant_value(neutral_int, t));
              }
            } else if (c == arg_0 && arg_id == 0 && test_i != c) {
              data.push_back(c);
            } else {
              data.push_back(c.as_instr()->args[arg_id]);
            }
          }
          worklist.emplace_back(result, std::move(data));
        }
        continue;
      }
      for (size_t arg_id = 0; arg_id < n_args; arg_id++) {
        TVec<fir::ValueR> data;
        for (auto c : curr) {
          data.push_back(c.as_instr()->args[arg_id]);
        }
        worklist.emplace_back(result, std::move(data));
      }
      continue;
    }
    if (test_i.is_constant()) {
      if (constant_match(curr)) {
        PlanNode n;
        n.kind = NodeKind::Constant;
        n.n_lanes = curr.size();
        ASSERT(parent != NoNode);
        n.insert_loc = plan.nodes[parent].insert_loc;
        n.lanes = curr;
        bool all_equal = true;
        for (auto v : curr) {
          all_equal &= v == curr[0];
        }
        n.shape = all_equal ? LaneShape::Uniform : LaneShape::Varying;
        add_node(parent, std::move(n));
      } else {
        if constexpr (debug_print) {
          fmt::println("constant tree op no match");
        }
        return false;
      }
      continue;
    }
    if constexpr (debug_print) {
      fmt::println("Failed tree vectorize at something like-> {:cd}",
                   curr.back());
    }
    return false;
  }
  return true;
}

void SLPVectorizer::continious_vector_store(SeedBundle &bundle,
                                            fir::ValueR value) {
  TVec<i128> offsets;
  // collect constant offsets
  for (const auto &data : bundle.data) {
    if (!data.instr->args[1].is_constant()) {
      ASSERT(false);
    }
    if (!data.a.is_invalid() ||
        (!data.b.is_invalid() &&
         (!data.b.is_constant() || !data.b.as_constant()->is_int()))) {
      ASSERT(false);
    }
    i128 consti = 0;
    if (data.b.is_constant()) {
      consti = data.b.as_constant()->as_int();
    }
    offsets.push_back(consti);
  }

  // check they are continious
  for (size_t i = 1; i < offsets.size(); i++) {
    if (offsets[i] - offsets[i - 1] != bundle.type->get_size()) {
      ASSERT(false);
    }
  }

  size_t last_indx = 0;
  fir::Instr last_instr;
  for (const auto &data : bundle.data) {
    auto bb = data.instr->parent;
    for (size_t i = 0; i < bb->instructions.size(); i++) {
      if (bb->instructions[i] == data.instr) {
        if (i > last_indx) {
          last_indx = i;
          last_instr = data.instr;
        }
        break;
      }
    }
  }

  fir::Builder bb{last_instr};
  bb.build_store(bundle.data[0].instr->args[0], value, false, false);
  for (auto &b : bundle.data) {
    b.instr.destroy();
  }
}

std::pair<SLPVectorizer::SeedInstrData, fir::ValueR>
SLPVectorizer::get_storeload_data(fir::Instr storeload) {
  SeedInstrData data;
  data.instr = storeload;
  if (!storeload->args[0].is_instr()) {
    return {data, storeload->args[0]};
  }
  auto arg = storeload->args[0].as_instr();
  if (arg->is(fir::BinaryInstrSubType::PtrAdd) && arg->args[1].is_constant()) {
    data.b = arg->args[1];
    return {data, arg->args[0]};
  }
  if (arg->is(fir::BinaryInstrSubType::IntMul)) {
    if (arg->args[0].is_instr() &&
        arg->args[0].as_instr()->is(fir::BinaryInstrSubType::PtrAdd)) {
      auto sub_val = arg->args[0].as_instr();
      data.a = arg->args[1];
      data.b = sub_val->args[1];
      return {data, sub_val->args[0]};
    }
    data.a = arg->args[1];
    return {data, arg->args[0]};
  }
  return {data, storeload->args[0]};
}

std::optional<SLPVectorizer::SeedBundle>
SLPVectorizer::find_successive_loads(fir::BasicBlock bb, size_t instr_id,
                                     AliasAnalyis &aa) {
  SeedBundle curr;
  auto [data, base] = get_storeload_data(bb->instructions[instr_id]);
  curr.base = base;
  curr.type = bb->instructions[instr_id].get_type();
  curr.data.push_back(data);

  TVec<fir::Instr> pot_aliasing_stores;

  for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
    auto instr = bb->instructions[i];
    if (instr->is(fir::InstrType::LoadInstr) && curr.type == instr.get_type() &&
        !instr->Atomic) {
      auto [sdata, sbase] = get_storeload_data(instr);
      // if (curr.base == sbase) {
      curr.data.push_back(sdata);
      // }
    } else if (instr->is(fir::InstrType::StoreInstr)) {
      bool might_alias = false;
      for (const auto &load : curr.data) {
        if (aa.alias(instr->args[0], load.instr->args[0]) !=
            AliasAnalyis::AAResult::NoAlias) {
          might_alias = true;
          break;
        }
      }
      if (might_alias) {
        break;
      }
      pot_aliasing_stores.push_back(instr);
    } else if (instr->pot_modifies_mem()) {
      // TODO if it writes we could use aliasing to still apply this
      break;
    }
  }
  // if we alias with anything just abort for now
  // TODO: could just remove the aliasing loads from the set
  // for (auto pot_stor : pot_aliasing_stores) {
  //   for (u32 lid = curr.data.size(); lid > 0; lid--) {
  //     if (aa.alias(pot_stor->args[0], curr.data[lid - 1].instr->args[0]) !=
  //         AliasAnalyis::AAResult::NoAlias) {
  //       curr.data.erase(curr.data.begin() + lid);
  //     }
  //   }
  // }
  if (curr.data.size() > 1) {
    return curr;
  }
  return {};
}

namespace {
/*
    To have proper and consistent order and not accidentially causing the order
    to be flipped by collecting them wrongly i decided to just do left and right
    reduction so add(x, add(x, ...)) and add(add(x, ...), x) and not other tree
    like structures.
  */
void left_red_search(TVec<fir::ValueR> &reduction_inputs, fir::ValueR curr_base,
                     u32 exp_sub_type) {
  if (!curr_base.is_instr()) {
    reduction_inputs.clear();
    return;
  }
  auto curr_i = curr_base.as_instr();

  if (curr_i->instr_type != fir::InstrType::BinaryInstr ||
      curr_i->subtype != exp_sub_type) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  if ((curr_i->args[0].is_instr() &&
       curr_i->args[0].as_instr()->instr_type == fir::InstrType::BinaryInstr &&
       curr_i->args[0].as_instr()->subtype == exp_sub_type) &&
      curr_i->args[1].is_instr()) {
    left_red_search(reduction_inputs, curr_i->args[0], exp_sub_type);
    reduction_inputs.push_back(curr_i->args[1]);
    return;
  }
  if (curr_i->args[0].is_instr() && curr_i->args[1].is_instr()) {
    reduction_inputs.push_back(curr_i->args[0]);
    reduction_inputs.push_back(curr_i->args[1]);
  } else {
    reduction_inputs.push_back(curr_base);
  }
}
void right_red_search(TVec<fir::ValueR> &reduction_inputs,
                      fir::ValueR curr_base, u32 exp_sub_type) {
  if (!curr_base.is_instr()) {
    reduction_inputs.clear();
    return;
  }
  auto curr_i = curr_base.as_instr();

  if (curr_i->instr_type != fir::InstrType::BinaryInstr ||
      curr_i->subtype != exp_sub_type) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  if ((curr_i->args[1].is_instr() &&
       curr_i->args[1].as_instr()->instr_type == fir::InstrType::BinaryInstr &&
       curr_i->args[1].as_instr()->subtype == exp_sub_type) &&
      curr_i->args[0].is_instr()) {
    right_red_search(reduction_inputs, curr_i->args[1], exp_sub_type);
    reduction_inputs.push_back(curr_i->args[0]);
    return;
  }
  if (curr_i->args[0].is_instr() && curr_i->args[1].is_instr()) {
    reduction_inputs.push_back(curr_i->args[1]);
    reduction_inputs.push_back(curr_i->args[0]);
  } else {
    reduction_inputs.push_back(curr_base);
  }
}

/*when the operation is associative we can just collect them in any order*/
// TODO: however we dont just want any order best case we would want to
// reorder them to improve chances for stuff like loads to be contininous
void red_search_assoc(TVec<fir::ValueR> &reduction_inputs,
                      fir::ValueR curr_base, u32 exp_sub_type) {
  ASSERT(curr_base.is_instr());
  auto curr_i = curr_base.as_instr();

  if (curr_i->instr_type != fir::InstrType::BinaryInstr ||
      curr_i->subtype != exp_sub_type) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  if (curr_i->args[0].is_instr() || curr_i->args[1].is_instr()) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  red_search_assoc(reduction_inputs, curr_i->args[0], exp_sub_type);
  red_search_assoc(reduction_inputs, curr_i->args[1], exp_sub_type);
}
} // namespace

std::optional<SLPVectorizer::SeedBundle>
SLPVectorizer::find_reduction(fir::BasicBlock bb, size_t instr_id,
                              AliasAnalyis &aa, const conf::CompConf &conf) {
  SeedBundle curr;
  (void)bb;
  (void)aa;
  (void)instr_id;
  auto base_instr = bb->instructions[instr_id];
  auto subtype = base_instr->subtype;

  if (!base_instr->is(fir::InstrType::BinaryInstr)) {
    return {};
  }
  auto binsub = static_cast<fir::BinaryInstrSubType>(subtype);
  bool isProd = binsub == fir::BinaryInstrSubType::FloatMul ||
                binsub == fir::BinaryInstrSubType::IntMul;
  bool isSum = binsub == fir::BinaryInstrSubType::FloatAdd ||
               binsub == fir::BinaryInstrSubType::IntAdd;
  bool isBool = binsub == fir::BinaryInstrSubType::Or ||
                binsub == fir::BinaryInstrSubType::And ||
                binsub == fir::BinaryInstrSubType::Xor;
  bool is_associative = binsub == fir::BinaryInstrSubType::IntMul ||
                        binsub == fir::BinaryInstrSubType::IntAdd || isBool;
  is_associative |= conf.optim.fltOpt.associative_math &&
                    (binsub == fir::BinaryInstrSubType::FloatMul ||
                     binsub == fir::BinaryInstrSubType::FloatAdd);
  if (!isProd && !isSum && !isBool) {
    return {};
  }

  // TODO: find bigger ones
  if (!base_instr->args[0].is_instr() || !base_instr->args[1].is_instr()) {
    return {};
  }
  {
    TVec<fir::ValueR> res1;
    TVec<fir::ValueR> res2;
    if (is_associative) {
      red_search_assoc(res1, fir::ValueR{base_instr}, subtype);
    } else {
      left_red_search(res1, fir::ValueR{base_instr}, subtype);
      right_red_search(res2, fir::ValueR{base_instr}, subtype);
    }
    auto max_size = std::max(res1.size(), res2.size());

    if (max_size == 2 || max_size == 4 ||
        (max_size == 8 && base_instr.get_type()->get_bitwidth() == 4)) {
      TVec<SeedInstrData> out;
      if (max_size == res1.size()) {
        for (auto r : res1) {
          out.push_back(SeedInstrData{.instr = r.as_instr(), .a = {}, .b = {}});
        }
      } else {
        for (auto r : res2) {
          out.push_back(SeedInstrData{.instr = r.as_instr(), .a = {}, .b = {}});
        }
      }
      return SeedBundle{.base = fir::ValueR{base_instr},
                        .type = base_instr.get_type(),
                        .data = out};
      // fmt::println("FOUND RED WITH SIZE {}", res.size());
    }
  }
  return {};
  // return SeedBundle{
  //     .base = fir::ValueR{base_instr},
  //     .type = base_instr.get_type(),
  //     .data = {
  //         SeedInstrData{
  //             .instr = base_instr->args[0].as_instr(), .a = {}, .b = {}},
  //         SeedInstrData{
  //             .instr = base_instr->args[1].as_instr(), .a = {}, .b = {}}}};
}

std::optional<SLPVectorizer::SeedBundle>
SLPVectorizer::find_successive_stores(fir::BasicBlock bb, size_t instr_id,
                                      AliasAnalyis &aa) {
  SeedBundle curr;
  auto [data, base] = get_storeload_data(bb->instructions[instr_id]);
  curr.base = base;
  curr.type = bb->instructions[instr_id].get_type();
  curr.data.push_back(data);

  // fmt::print("===============Col store================\n{}\n",
  //            bb->instructions[instr_id]);
  for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
    auto instr = bb->instructions[i];
    if (instr->is(fir::InstrType::StoreInstr) &&
        curr.type == instr.get_type() && !instr->Atomic) {
      auto [sdata, sbase] = get_storeload_data(instr);
      // fmt::println("{} =?= {} => {}\n", curr.base, sbase, curr.base ==
      // sbase);
      if (curr.base == sbase) {
        curr.data.push_back(sdata);
      } else if (aa.alias(curr.base, sbase) !=
                 AliasAnalyis::AAResult::NoAlias) {
        // fmt::println("Failed aliasing store {}\n", instr);
        break;
      }
    } else if (instr->is(fir::InstrType::LoadInstr)) {
      // this load could load one of the previous stores
      //  if that is the case we cannot simply transform it into vector code
      //  since it would need to be placed before this load
      // TODO: we could transform it but leave the store that is associated
      // to this load alive to keep it correct? (would need to check if
      // thats even worth it)
      bool might_alias = false;
      for (const auto &s : curr.data) {
        if (aa.alias(instr->args[0], s.instr->args[0]) !=
            AliasAnalyis::AAResult::NoAlias) {
          might_alias = true;
          break;
        }
      }
      if (might_alias) {
        // fmt::println("Failed aliasing load {}\n", instr);
        break;
      }
    } else if (instr->pot_modifies_mem() || instr->pot_reads_mem()) {
      // fmt::println("Failed pot aliasing operation {}\n", instr);
      // TODO if it writes we could use aliasing to still apply this
      break;
    }
  }
  if (curr.data.size() > 1) {
    return curr;
  }
  return {};
}
namespace {
using SeedBundle = SLPVectorizer::SeedBundle;

void sort_by_size(TVec<SeedBundle> &bundles) {
  std::ranges::sort(bundles, [](const SeedBundle &b1, const SeedBundle &b2) {
    return b1.data.size() > b2.data.size();
  });
}

void finalize_loads(TVec<SeedBundle> &load_bundles) {
  sort_by_size(load_bundles);
  for (auto bi = load_bundles.size(); bi > 0; bi--) {
    auto &b = load_bundles[bi - 1];
    {
      // sort the loads
      std::ranges::sort(b.data, [](const auto &a, const auto &b) {
        i128 av = 0;
        i128 bv = 0;
        if (a.b.is_constant() && a.b.as_constant()->is_int()) {
          av = a.b.as_constant()->as_int();
        }
        if (b.b.is_constant() && b.b.as_constant()->is_int()) {
          bv = b.b.as_constant()->as_int();
        }
        return av < bv;
      });
    }
  }
  // fmt::println("F1 {}  {}", store_bundles.size(), load_bundles.size());
}

void finalize_reductions(TVec<SeedBundle> &reduction_bundles) {
  sort_by_size(reduction_bundles);
  for (auto bi = reduction_bundles.size(); bi > 0; bi--) {
    auto &b = reduction_bundles[bi - 1];
    {
      auto n_load = b.data.size();
      if (n_load != 2 && n_load != 4 && n_load != 8 && n_load != 16 &&
          n_load != 32) {
        if (n_load > 32) {
          b.data.resize(32);
        } else if (n_load > 16) {
          b.data.resize(16);
        } else if (n_load > 8) {
          b.data.resize(8);
        } else if (n_load > 4) {
          b.data.resize(4);
        } else if (n_load > 2) {
          b.data.resize(2);
        } else {
          reduction_bundles.erase(reduction_bundles.begin() +
                                  static_cast<i64>(bi) - 1);
          continue;
        }
      }
      if (b.type->get_size() == 1 &&
          (n_load % 8 != 0 && n_load % 16 != 0 && n_load % 32 != 0)) {
        reduction_bundles.erase(reduction_bundles.begin() +
                                static_cast<i64>(bi) - 1);
        continue;
      }
      if (b.type->get_size() == 2 && (n_load % 8 != 0 && n_load % 16 != 0)) {
        reduction_bundles.erase(reduction_bundles.begin() +
                                static_cast<i64>(bi) - 1);
        continue;
      }
      if (b.type->get_size() == 4 && (n_load % 4 != 0 && n_load % 8 != 0)) {
        reduction_bundles.erase(reduction_bundles.begin() +
                                static_cast<i64>(bi) - 1);
        continue;
      }
      if (b.type->get_size() == 8 && (n_load % 2 != 0 && n_load % 4 != 0)) {
        reduction_bundles.erase(reduction_bundles.begin() +
                                static_cast<i64>(bi) - 1);
        continue;
      }

      // TODO disable them for now
      if (b.type->get_size() == 1 || b.type->get_size() == 2) {
        reduction_bundles.erase(reduction_bundles.begin() + static_cast<i64>(bi) - 1);
        continue;
      }

      // sort the loads
      std::ranges::sort(b.data, [](const auto &a, const auto &b) {
        i128 av = 0;
        i128 bv = 0;
        if (a.b.is_constant() && a.b.as_constant()->is_int()) {
          av = a.b.as_constant()->as_int();
        }
        if (b.b.is_constant() && b.b.as_constant()->is_int()) {
          bv = b.b.as_constant()->as_int();
        }
        return av < bv;
      });
    }
  }
}

void finalize_stores(TVec<SeedBundle> &store_bundles,
                     const conf::CompConf &conf) {
  sort_by_size(store_bundles);
  for (auto bi = store_bundles.size(); bi > 0; bi--) {
    auto &b = store_bundles[bi - 1];
    {
      auto n_stor = b.data.size();
      if (n_stor != 2 && n_stor != 4 && n_stor != 8 && n_stor != 16 &&
          n_stor != 32) {
        // just cut it off 4head
        if (n_stor > 32) {
          b.data.resize(32);
        } else if (n_stor > 16) {
          b.data.resize(16);
        } else if (n_stor > 8) {
          b.data.resize(8);
        } else if (n_stor > 4) {
          b.data.resize(4);
        } else if (n_stor > 2) {
          b.data.resize(2);
        } else {
          store_bundles.erase(store_bundles.begin() + static_cast<i64>(bi) - 1);
          continue;
        }
      }
      auto overall_width = n_stor * b.type->get_bitwidth();
      while (overall_width > 512 ||
             (!conf.target.features.avx512f && overall_width > 256)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }

      while (b.type->get_size() == 1 &&
             (n_stor % 16 != 0 && n_stor % 32 != 0)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }
      while (b.type->get_size() == 2 && (n_stor % 8 != 0 && n_stor % 16 != 0)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }
      while (b.type->get_size() == 4 && (n_stor % 4 != 0 && n_stor % 8 != 0)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }
      while (b.type->get_size() == 8 && (n_stor % 2 != 0 && n_stor % 4 != 0)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }
      // in case we need it earlier we want to make sure its correct for now
      // even tough the compiler is unhappy that i dont read it
      (void)overall_width;

      // TODO disable them for now
      if (b.type->get_size() == 1 || b.type->get_size() == 2) {
        store_bundles.erase(store_bundles.begin() + static_cast<i64>(bi) - 1);
        continue;
      }
      auto c1 = b.type->get_size() == 1 && n_stor < 16;
      auto c2 = b.type->get_size() == 2 && n_stor < 8;
      auto c3 = b.type->get_size() == 4 && n_stor < 4;
      auto c4 = b.type->get_size() == 8 && n_stor < 2;

      if (c1 || c2 || c3 || c4) {
        store_bundles.erase(store_bundles.begin() + static_cast<i64>(bi) - 1);
        continue;
      }

      // sort the stores
      std::ranges::sort(b.data, [](const auto &a, const auto &b) {
        i128 av = 0;
        i128 bv = 0;
        if (a.b.is_constant() && a.b.as_constant()->is_int()) {
          av = a.b.as_constant()->as_int();
        }
        if (b.b.is_constant() && b.b.as_constant()->is_int()) {
          bv = b.b.as_constant()->as_int();
        }
        return av < bv;
      });
      // check if continious
      {
        TVec<i128> offsets;
        // collect constant offsets
        bool failed = false;
        for (const auto &data : b.data) {
          if (!data.a.is_invalid() ||
              (!data.b.is_invalid() &&
               (!data.b.is_constant() || !data.b.as_constant()->is_int()))) {
            failed = true;
            break;
          }
          i128 consti = 0;
          if (data.b.is_constant()) {
            consti = data.b.as_constant()->as_int();
          }
          offsets.push_back(consti);
        }
        if (failed) {
          store_bundles.erase(store_bundles.begin() + static_cast<i64>(bi) - 1);
          continue;
        }

        // check they are continious
        for (size_t i = 1; i < offsets.size(); i++) {
          if (offsets[i] - offsets[i - 1] != b.type->get_size()) {
            failed = true;
            break;
          }
        }
        if (failed) {
          store_bundles.erase(store_bundles.begin() + static_cast<i64>(bi) - 1);
          continue;
        }
      }
    }
  }
}

class StoreSeedProvider final : public SLPVectorizer::SeedProvider {
public:
  void visit(SLPVectorizer::SeedRegion &r, size_t i) final {
    if (r.bb->instructions[i]->is(fir::InstrType::StoreInstr)) {
      if (auto res = SLPVectorizer::find_successive_stores(r.bb, i, r.aa)) {
        bundles.push_back(res.value());
      }
    }
  }
  void finalize(const conf::CompConf &conf) final {
    finalize_stores(bundles, conf);
  }
};

// load bundles are no seeds, they only tell the tree builder which loads are
// adjacent
class LoadSeedProvider final : public SLPVectorizer::SeedProvider {
public:
  void visit(SLPVectorizer::SeedRegion &r, size_t i) final {
    if (r.bb->instructions[i]->is(fir::InstrType::LoadInstr)) {
      if (auto res = SLPVectorizer::find_successive_loads(r.bb, i, r.aa)) {
        bundles.push_back(res.value());
      }
    }
  }
  void finalize(const conf::CompConf & /*conf*/) final {
    finalize_loads(bundles);
  }
};

class ReductionSeedProvider final : public SLPVectorizer::SeedProvider {
public:
  void visit(SLPVectorizer::SeedRegion &r, size_t i) final {
    if (!r.reductions) {
      return;
    }
    if (auto res = SLPVectorizer::find_reduction(r.bb, i, r.aa, r.conf)) {
      bundles.push_back(res.value());
    }
  }
  void finalize(const conf::CompConf & /*conf*/) final {
    finalize_reductions(bundles);
  }
};
} // namespace

void SLPVectorizer::find_seeds(const conf::CompConf &conf, fir::BasicBlock bb,
                               TVec<SeedBundle> &store_bundles,
                               TVec<SeedBundle> &load_bundles,
                               TVec<SeedBundle> &reduction_bundles,
                               AliasAnalyis &aa) {
  // load bundles are collected too since that simplifies checking later on if
  // a load is valid to vectorize
  StoreSeedProvider stores;
  LoadSeedProvider loads;
  ReductionSeedProvider reductions;
  SeedProvider *providers[] = {&stores, &loads, &reductions};
  SeedRegion region{.bb = bb, .aa = aa, .conf = conf,
                    .reductions = config.reductions};
  for (size_t i = 0; i < bb->instructions.size(); i++) {
    for (auto *p : providers) {
      p->visit(region, i);
    }
  }
  auto &stats = utils::StatCollector::get();
  const char *seen_names[] = {"SLPSeedsStore", "SLPSeedsLoad",
                              "SLPSeedsReduction"};
  const char *rejected_names[] = {"SLPSeedsRejectedStore",
                                  "SLPSeedsRejectedLoad",
                                  "SLPSeedsRejectedReduction"};
  size_t idx = 0;
  for (auto *p : providers) {
    auto found = p->bundles.size();
    p->finalize(conf);
    stats.addi(static_cast<i64>(found), seen_names[idx],
               utils::StatCollector::StatFOptim);
    stats.addi(static_cast<i64>(found - std::min(found, p->bundles.size())),
               rejected_names[idx], utils::StatCollector::StatFOptim);
    idx++;
  }
  store_bundles = std::move(stores.bundles);
  load_bundles = std::move(loads.bundles);
  reduction_bundles = std::move(reductions.bundles);
}

bool SLPVectorizer::constant_vector_store(fir::Context &ctx,
                                          SeedBundle &bundle) {
  IRVec<fir::ConstantValueR> args;
  for (const auto &data : bundle.data) {
    if (!data.instr->args[1].is_constant()) {
      return false;
    }
  }
  for (const auto &b : bundle.data) {
    args.push_back(b.instr->args[1].as_constant());
  }
  auto constant_data = ctx->get_constant_value(
      std::move(args), ctx->get_vec_type(bundle.type, bundle.data.size()));
  continious_vector_store(bundle, fir::ValueR{constant_data});
  return true;
}

} // namespace foptim::optim
