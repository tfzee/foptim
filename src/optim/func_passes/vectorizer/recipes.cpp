#include "optim/func_passes/vectorizer/recipes.hpp"

#include <array>
#include <bit>

#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/func_passes/vectorizer/seeds.hpp"

namespace foptim::optim::vec {

namespace {
using BinSub = fir::BinaryInstrSubType;

bool is_i1(fir::TypeR ty) { return ty->is_int() && ty->as_int() == 1; }

// the lane variants most node kinds pick their instruction from: the last lane
// if it is an instruction, otherwise the first one
fir::Instr pick_instr(const TVec<fir::ValueR> &values) {
  return lane_instr(values.back()) ? values.back().as_instr()
                                   : values.front().as_instr();
}

// The widened code of a node is emitted next to the program-last lane in the
// seed block: every operand of every lane is defined before its lane, so it
// dominates that point, and the (already emitted) operand nodes sit before it.
// Lanes that are not instructions of the seed block (constants, arguments,
// values of other blocks) dominate all of the seed block anyway. If there is no
// lane in the block the node goes in front of its parent.
fir::Instr last_lane(const TVec<fir::ValueR> &values, const InitCtx &c) {
  fir::Instr best = c.parent_loc;
  std::optional<u32> best_pos;
  for (auto v : values) {
    if (!lane_instr(v)) {
      continue;
    }
    auto p = c.info.order.pos(v.as_instr());
    if (p && (!best_pos || *p > *best_pos)) {
      best = v.as_instr();
      best_pos = p;
    }
  }
  ASSERT(best.is_valid());
  return best;
}
void init_flex(PlanNode &n, const TVec<fir::ValueR> &values, const InitCtx &c) {
  n.n_lanes = values.size();
  auto pick = pick_instr(values);
  n.insert_loc = last_lane(values, c);
  n.type = pick->result_type();
  n.sub = pick->subtype;
}
bool same_kind_instrs(const TVec<fir::ValueR> &values, fir::Instr base_v) {
  for (auto i_v : values) {
    if (!lane_instr(i_v)) {
      return false;
    }
    auto i = i_v.as_instr();
    if (i->instr_type != base_v->instr_type || i->subtype != base_v->subtype) {
      return false;
    }
  }
  return true;
}

size_t n_args_1(const TVec<fir::ValueR> & /*values*/) { return 1; }
size_t n_args_0(const TVec<fir::ValueR> & /*values*/) { return 0; }
size_t n_args_2(const TVec<fir::ValueR> & /*values*/) { return 2; }
size_t n_args_instr(const TVec<fir::ValueR> &values) {
  return values[0].as_instr()->get_n_args();
}

bool match_same_instr(const TVec<fir::ValueR> &values, BundleCtx & /*ctx*/) {
  return same_kind_instrs(values, pick_instr(values));
}
bool match_same_instr_back(const TVec<fir::ValueR> &values,
                           BundleCtx & /*ctx*/) {
  return lane_instr(values.back()) &&
         same_kind_instrs(values, values.back().as_instr());
}

bool always_legal(const PlanNode & /*n*/, LaneSpace /*space*/, u32 /*vf*/,
                  const Features & /*features*/) {
  return true;
}

bool store_match_bundle(const TVec<fir::ValueR> & /*values*/,
                        BundleCtx & /*ctx*/) {
  return true;
}
void store_init(PlanNode &n, const TVec<fir::ValueR> &values,
                const InitCtx &c) {
  init_back(n, values, c);
  n.aux = values[0].as_instr();
  n.shape = LaneShape::Consecutive;
}

// unit stride access means the address steps by the element size each iteration
bool unit_stride(const LoopCtx &c, fir::ValueR ptr, u32 size) {
  const auto r = c.evo.get(ptr);
  return c.evo.is_addrec(r) && c.evo.is_const(c.evo.expr(r).b) &&
         c.evo.expr(c.evo.expr(r).b).cval_i() == static_cast<i128>(size);
}
// children are in reverse operand order, operands that are not part of the
// plan (loop invariant values) have no node
void loop_children(const LoopCtx &c, PlanNode &n,
                   std::span<const fir::ValueR> operands) {
  for (size_t i = operands.size(); i > 0; i--) {
    const u32 o = shape_node(c, operands[i - 1]);
    if (o != ~0U) {
      n.children.push_back(o);
    }
  }
}
// Strided operands would need an iota vector
bool vector_operand_ok(const LoopCtx &c, fir::ValueR v) {
  return shape_of(c, v) != LaneShape::Strided;
}

bool store_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->args[1].get_type();
  if (!elem_type_ok(ty) || !unit_stride(c, i->args[0], ty->get_size()) ||
      !vector_operand_ok(c, i->args[1])) {
    return false;
  }
  if (shape_node(c, i->args[0]) == ~0U) {
    return false;
  }
  n.kind = NodeKind::Store;
  n.shape = LaneShape::Varying;
  n.type = ty;
  loop_children(c, n, std::array{i->args[0], i->args[1]});
  return true;
}

bool binary_sub_ok(BinSub sub) {
  switch (sub) {
    case BinSub::IntAdd:
    case BinSub::IntSub:
    case BinSub::IntMul:
    case BinSub::Shl:
    case BinSub::Shr:
    case BinSub::AShr:
    case BinSub::And:
    case BinSub::Or:
    case BinSub::Xor:
    case BinSub::FloatAdd:
    case BinSub::FloatSub:
    case BinSub::FloatMul:
    case BinSub::FloatDiv:
      return true;
    default:
      // division could trap in lanes the scalar loop never runs
      return false;
  }
}

bool binary_match_bundle(const TVec<fir::ValueR> &values, BundleCtx &ctx) {
  auto base_v = pick_instr(values);
  for (auto i_v : values) {
    if (i_v == base_v->args[0]) {
      ctx.potential_neutral = true;
      continue;
    }
    if (!lane_instr(i_v)) {
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
  switch (static_cast<BinSub>(base_v->subtype)) {
    case BinSub::FloatAdd:
    case BinSub::IntAdd:
    case BinSub::PtrAdd:
    case BinSub::Shl:
    case BinSub::IntSub:
    case BinSub::Xor:
    case BinSub::Or:
    case BinSub::Shr:
    case BinSub::AShr:
      return true;
    case BinSub::FloatSub:
    case BinSub::FloatMul:
    case BinSub::IntMul:
    case BinSub::And:
    case BinSub::FloatDiv:
      // no neutral element to pass a lane without the operation through
      // (div has one for the right operand only)
      return !ctx.potential_neutral;
    case BinSub::IntSRem:
    case BinSub::IntURem:
    case BinSub::IntSDiv:
    case BinSub::IntUDiv:
      return false;
    case BinSub::INVALID:
      fmt::println("{}", base_v);
      TODO("impl");
      return false;
  }
}

bool binary_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->result_type();
  if (!elem_type_ok(ty) || !binary_sub_ok(static_cast<BinSub>(i->subtype)) ||
      !vector_operand_ok(c, i->args[0]) || !vector_operand_ok(c, i->args[1])) {
    return false;
  }
  // byte and word shifts exist only for one amount in every lane
  if (ty->is_int() && ty->get_bitwidth() < 32 &&
      (i->subtype == static_cast<u32>(BinSub::Shl) ||
       i->subtype == static_cast<u32>(BinSub::Shr) ||
       i->subtype == static_cast<u32>(BinSub::AShr))) {
    if (is_vector_shape(shape_of(c, i->args[1]))) {
      return false;
    }
    if (i->args[1].is_constant_int() &&
        i->args[1].as_constant()->as_apint().uge(
            APInt(i->args[1].as_constant()->as_apint().bits(),
                  u64{ty->get_bitwidth()}))) {
      return false;
    }
  }
  // masks are never broadcast, so both operands have to be vectors
  if (ty->is_int() && ty->as_int() == 1 &&
      !(is_vector_shape(shape_of(c, i->args[0])) &&
        is_vector_shape(shape_of(c, i->args[1])))) {
    return false;
  }
  n.kind = NodeKind::Binary;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.sub = i->subtype;
  loop_children(c, n, std::array{i->args[0], i->args[1]});
  return true;
}

bool binary_legal(const PlanNode &n, LaneSpace space, u32 /*vf*/,
                  const Features &features) {
  const auto sub = static_cast<BinSub>(n.sub);
  // the amount of a narrow shift is the same in every lane, checked by
  // match_loop
  const bool shift =
      sub == BinSub::Shl || sub == BinSub::Shr || sub == BinSub::AShr;
  if (n.type->is_int() && n.type->get_bitwidth() < 32 &&
      ((shift && space != LaneSpace::LoopIter) ||
       (sub == BinSub::IntMul && n.type->get_bitwidth() == 8))) {
    return false;
  }
  // no 64 bit vector multiply before avx512dq, a bundle pays for the emulation
  // in the cost instead
  if (space == LaneSpace::LoopIter && sub == BinSub::IntMul &&
      n.type->get_bitwidth() == 64 && !features.avx512dq) {
    return false;
  }
  return true;
}
i64 binary_cost(const PlanNode &n, u32 /*vf*/, const Features &features) {
  const auto sub = static_cast<BinSub>(n.sub);
  if (sub == BinSub::IntMul && n.type->is_int()) {
    if (n.type->get_bitwidth() == 64 && !features.avx512dq) {
      return 8;
    }
    if (n.type->get_bitwidth() == 32) {
      return 2;
    }
  }
  if (sub == BinSub::FloatDiv) {
    return 6;
  }
  return 1;
}

bool load_match_bundle(const TVec<fir::ValueR> &values, BundleCtx &ctx) {
  for (auto v : values) {
    if (!lane_instr(v)) {
      return false;
    }
  }
  auto base_v = values.back().as_instr();
  if (base_v->result_type()->is_vec()) {
    return false;
  }
  for (auto i_v : values) {
    auto i = i_v.as_instr();
    if (i->instr_type != base_v->instr_type || i->subtype != base_v->subtype ||
        i->result_type() != base_v->result_type()) {
      return false;
    }
  }
  // the loads are all emitted at the last one, so they have to be plain
  // loads of the seed block that can move there
  const auto &mem = ctx.info.mem();
  TVec<u32> members;
  for (auto v : values) {
    auto idx = mem.index_of(v.as_instr());
    if (!idx || mem.accesses()[*idx].kind != BlockMemAccess::Kind::Load) {
      return false;
    }
    members.push_back(*idx);
  }
  return mem.can_sink(members, ctx.aa) == MemDep::Independent;
}
bool load_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->result_type();
  if (!elem_type_ok(ty) || !unit_stride(c, i->args[0], ty->get_size())) {
    return false;
  }
  if (shape_node(c, i->args[0]) == ~0U) {
    return false;
  }
  n.kind = NodeKind::Load;
  n.shape = LaneShape::Varying;
  n.type = ty;
  loop_children(c, n, std::array{i->args[0]});
  return true;
}
// the vpgather forms of avx2 (index vectors of 32 bit are used where the
// instruction takes them, 64 bit for two 64 bit lanes)
bool gather_load_legal(u32 elem_bits, u32 lanes) {
  return (elem_bits == 32 && (lanes == 4 || lanes == 8)) ||
         (elem_bits == 64 && (lanes == 2 || lanes == 4));
}
bool load_legal(const PlanNode &n, LaneSpace space, u32 /*vf*/,
                const Features &features) {
  if (space == LaneSpace::LoopIter) {
    return true;
  }
  // TODO dont know how to grather i8 and stuff efficiently
  if (n.type->get_bitwidth() <= 16) {
    return false;
  }
  if (!n.shuf_mask.empty()) {
    return true;
  }
  if (!n.gather_idx.empty()) {
    return gather_load_legal(n.type->get_bitwidth(), n.n_lanes);
  }
  // TODO impl vec for bigger sizes
  return !(n.n_lanes > 8 || (!features.avx512f && n.n_lanes > 4));
}
bool consecutive_loads(const TVec<fir::ValueR> &values) {
  PtrShape first;
  for (size_t i = 0; i < values.size(); i++) {
    auto instr = values[i].as_instr();
    auto shape = decompose_ptr(instr->args[0]);
    if (i == 0) {
      first = shape;
    } else if (!shape.same_base(first) ||
               shape.offset !=
                   first.offset +
                       static_cast<i64>(i) * instr.result_type()->get_size()) {
      return false;
    }
  }
  return true;
}
// Indices within a window of at most two vectors become one or two vector
// loads and a shuffle. The loads stay between the lowest and the highest index
// so nothing outside the accessed range is read.
void shuffle_load_init(PlanNode &n) {
  const u32 bits = n.type->get_bitwidth();
  const i64 lanes = n.n_lanes;
  if (!((bits == 32 && lanes == 4) || (bits == 64 && lanes == 2))) {
    return;
  }
  const auto [lo_it, hi_it] = std::ranges::minmax_element(n.gather_idx);
  const i64 lo = *lo_it;
  const i64 hi = *hi_it;
  const i64 span = hi - lo + 1;
  if (span < lanes || span > 2 * lanes) {
    return;
  }
  TVec<i64> starts{lo};
  if (span > lanes) {
    starts.push_back(hi - lanes + 1);
  }
  TVec<u32> mask;
  TVec<u32> source;
  for (auto i : n.gather_idx) {
    const bool first = i < lo + lanes;
    source.push_back(first ? 0 : 1);
    mask.push_back(first ? i - lo : lanes + (i - starts[1]));
  }
  if (starts.size() == 2) {
    // the backend only has vshufps for two sources
    if (bits != 32 || source[0] != source[1] || source[2] != source[3] ||
        source[0] == source[2]) {
      return;
    }
  }
  n.shuf_starts = std::move(starts);
  n.shuf_mask = std::move(mask);
}
// lanes that read the anchor plus a constant multiple of the element size
// become one gather with an index vector
void gather_load_init(PlanNode &n, const TVec<fir::ValueR> &values) {
  fir::ValueR base0;
  const i64 elem = values[0].as_instr()->result_type()->get_size();
  TVec<i64> idx;
  for (size_t i = 0; i < values.size(); i++) {
    auto shape = decompose_ptr(values[i].as_instr()->args[0]);
    if (i == 0) {
      base0 = shape.anchor;
    } else if (shape.anchor != base0) {
      return;
    }
    const i64 offset = shape.anchor_offset;
    if (offset % elem != 0 || offset / elem > INT32_MAX ||
        offset / elem < INT32_MIN) {
      return;
    }
    idx.push_back(offset / elem);
  }
  n.value = base0;
  n.gather_idx = std::move(idx);
  shuffle_load_init(n);
}
void load_init(PlanNode &n, const TVec<fir::ValueR> &values, const InitCtx &c) {
  init_back(n, values, c);
  n.aux = values[0].as_instr();
  n.shape =
      consecutive_loads(values) ? LaneShape::Consecutive : LaneShape::Varying;
  if (n.shape == LaneShape::Varying) {
    gather_load_init(n, values);
  }
}
i64 load_cost(const PlanNode &n, u32 vf, const Features & /*features*/) {
  // loop loads are unit stride and have no lanes set
  const bool gather = n.n_lanes != 0 && n.shape != LaneShape::Consecutive;
  if (!n.shuf_mask.empty()) {
    return static_cast<i64>(n.shuf_starts.size()) + 1;
  }
  if (!n.gather_idx.empty()) {
    if (std::ranges::all_of(n.gather_idx,
                            [&](i64 i) { return i == n.gather_idx[0]; })) {
      return 2;
    }
    return static_cast<i64>(vf) + 2;
  }
  return gather ? 3 * static_cast<i64>(vf) : 1;
}

bool unary_match(const TVec<fir::ValueR> &values, BundleCtx &ctx) {
  if (!match_same_instr_back(values, ctx)) {
    return false;
  }
  switch (
      static_cast<fir::UnaryInstrSubType>(values.back().as_instr()->subtype)) {
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

bool unary_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->result_type();
  switch (static_cast<fir::UnaryInstrSubType>(i->subtype)) {
    case fir::UnaryInstrSubType::Not:
      if (!ty->is_int() ||
          (is_i1(ty) && !is_vector_shape(shape_of(c, i->args[0])))) {
        return false;
      }
      break;
    case fir::UnaryInstrSubType::FloatNeg:
    case fir::UnaryInstrSubType::FloatSqrt:
      if (!ty->is_float()) {
        return false;
      }
      break;
    default:
      return false;
  }
  if (!elem_type_ok(ty) || !vector_operand_ok(c, i->args[0])) {
    return false;
  }
  n.kind = NodeKind::Unary;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.sub = i->subtype;
  loop_children(c, n, std::array{i->args[0]});
  return true;
}

bool extract_match(const TVec<fir::ValueR> &values, BundleCtx & /*ctx*/) {
  if (!lane_instr(values.back())) {
    return false;
  }
  auto base_v = values.back().as_instr();
  auto exp_off = 0;
  auto input_val = base_v->args[0];
  // the lanes have to be all of the vector
  if (!input_val.get_type()->is_vec() ||
      input_val.get_type()->as_vec().member_number != values.size()) {
    return false;
  }
  for (auto i_v : values) {
    if (!lane_instr(i_v)) {
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
// the lanes can be in another block than the seed, so remember the last one
void extract_init(PlanNode &n, const TVec<fir::ValueR> &values,
                  const InitCtx &c) {
  init_back(n, values, c);
  n.value = values.back();
}

bool call_match(const TVec<fir::ValueR> &values, BundleCtx & /*ctx*/) {
  auto base_v = pick_instr(values);
  if (!base_v->args[0].is_constant_func()) {
    return false;
  }
  auto called_func = base_v->args[0].as_constant()->as_func();
  if (!called_func->attribs.maybe_can_wfvec) {
    return false;
  }
  for (auto i_v : values) {
    if (!lane_instr(i_v)) {
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
void call_init(PlanNode &n, const TVec<fir::ValueR> &values, const InitCtx &c) {
  init_back(n, values, c);
  n.value = values.back().as_instr()->args[0];
}

// Lane width of the vector compares a mask is built from, 0 if the value is
// not a compare or logic of compares of one width. A mask lives in a vector
// register of lanes of that width, so everything that combines masks has to
// agree on it.
}  // namespace

u32 mask_bits_of(fir::ValueR v) {
  if (!lane_instr(v) || !is_i1(v.as_instr()->result_type())) {
    return 0;
  }
  auto i = v.as_instr();
  if (i->is(fir::InstrType::ICmp) || i->is(fir::InstrType::FCmp)) {
    const auto t = i->args[0].get_type();
    return t->is_int() || t->is_float() ? t->get_bitwidth() : 0;
  }
  if (i->is(fir::InstrType::BinaryInstr)) {
    const auto sub = static_cast<BinSub>(i->subtype);
    if (sub != BinSub::And && sub != BinSub::Or && sub != BinSub::Xor) {
      return 0;
    }
    const u32 a = mask_bits_of(i->args[0]);
    return a == mask_bits_of(i->args[1]) ? a : 0;
  }
  if (i->is(fir::UnaryInstrSubType::Not)) {
    return mask_bits_of(i->args[0]);
  }
  return 0;
}

// the vector forms of compares and selects only exist up to 256 bit
bool mask_width_ok(u32 bits, u32 vf) {
  return (bits == 8 || bits == 16 || bits == 32 || bits == 64) &&
         vf * bits <= 256;
}

namespace {

bool cmp_match_bundle(const TVec<fir::ValueR> &values, BundleCtx &ctx) {
  if (!match_same_instr_back(values, ctx)) {
    return false;
  }
  const auto ty = values.back().as_instr()->args[0].get_type();
  if (!ty->is_int() && !ty->is_float()) {
    return false;
  }
  for (auto v : values) {
    auto i = v.as_instr();
    if (i->args[0].get_type() != ty || i->args[1].get_type() != ty) {
      return false;
    }
  }
  return true;
}
void cmp_init(PlanNode &n, const TVec<fir::ValueR> &values, const InitCtx &c) {
  init_back(n, values, c);
  n.op_type = values.back().as_instr()->args[0].get_type();
}
bool cmp_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->args[0].get_type();
  if (!elem_type_ok(ty) || i->args[1].get_type() != ty ||
      !vector_operand_ok(c, i->args[0]) || !vector_operand_ok(c, i->args[1])) {
    return false;
  }
  n.kind = NodeKind::Cmp;
  n.shape = LaneShape::Varying;
  n.type = i->result_type();
  n.op_type = ty;
  n.sub = i->subtype;
  loop_children(c, n, std::array{i->args[0], i->args[1]});
  return true;
}
bool cmp_legal(const PlanNode &n, LaneSpace /*space*/, u32 vf,
               const Features & /*features*/) {
  return mask_width_ok(n.op_type->get_bitwidth(), vf);
}
i64 cmp_cost(const PlanNode &n, u32 /*vf*/, const Features & /*features*/) {
  if (n.op_type->is_float()) {
    return 1;
  }
  // only equal and signed greater than exist, the rest takes a second step
  switch (static_cast<fir::ICmpInstrSubType>(n.sub)) {
    case fir::ICmpInstrSubType::EQ:
    case fir::ICmpInstrSubType::SGT:
    case fir::ICmpInstrSubType::SLT:
      return 1;
    default:
      return 2;
  }
}

bool select_match_bundle(const TVec<fir::ValueR> &values, BundleCtx &ctx) {
  if (!match_same_instr(values, ctx)) {
    return false;
  }
  const auto ty = values.back().as_instr()->result_type();
  if (!ty->is_int() && !ty->is_float()) {
    return false;
  }
  for (auto v : values) {
    if (mask_bits_of(v.as_instr()->args[0]) != ty->get_bitwidth()) {
      return false;
    }
  }
  return true;
}
bool select_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->result_type();
  if (!elem_type_ok(ty) || !is_vector_shape(shape_of(c, i->args[0])) ||
      mask_bits_of(i->args[0]) != ty->get_bitwidth() ||
      !vector_operand_ok(c, i->args[1]) || !vector_operand_ok(c, i->args[2])) {
    return false;
  }
  n.kind = NodeKind::Select;
  n.shape = LaneShape::Varying;
  n.type = ty;
  loop_children(c, n, std::array{i->args[0], i->args[1], i->args[2]});
  return true;
}
bool select_legal(const PlanNode &n, LaneSpace /*space*/, u32 vf,
                  const Features & /*features*/) {
  return mask_width_ok(n.type->get_bitwidth(), vf);
}

// a zext of a mask is not a vector operation
bool zext_match(const TVec<fir::ValueR> &values, BundleCtx &ctx) {
  return match_same_instr(values, ctx) &&
         !is_i1(values.back().as_instr()->args[0].get_type());
}
// widening of integer lanes, the source lanes have to be a vector already
bool widen_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n, bool sign) {
  const auto ty = i->result_type();
  const auto src = i->args[0].get_type();
  if (!ty->is_int() || !src->is_int() || is_i1(src) || is_i1(ty) ||
      !is_vector_shape(shape_of(c, i->args[0])) ||
      (src->get_bitwidth() != 8 && src->get_bitwidth() != 16 &&
       src->get_bitwidth() != 32) ||
      (ty->get_bitwidth() != 16 && ty->get_bitwidth() != 32 &&
       ty->get_bitwidth() != 64) ||
      ty->get_bitwidth() <= src->get_bitwidth()) {
    return false;
  }
  n.kind = NodeKind::Widen;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.op_type = src;
  n.sub = sign ? 1 : 0;
  loop_children(c, n, std::array{i->args[0]});
  return true;
}
// a mask widened to 32 or 64 bit lanes of the same width is 0 or 1
bool zext_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->result_type();
  if (!is_i1(i->args[0].get_type())) {
    return widen_match_loop(c, i, n, false);
  }
  if (!is_vector_shape(shape_of(c, i->args[0])) ||
      mask_bits_of(i->args[0]) != ty->get_bitwidth() ||
      (ty->get_bitwidth() != 32 && ty->get_bitwidth() != 64)) {
    return false;
  }
  n.kind = NodeKind::Zext;
  n.shape = LaneShape::Varying;
  n.type = ty;
  loop_children(c, n, std::array{i->args[0]});
  return true;
}
bool sext_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  return widen_match_loop(c, i, n, true);
}
bool no_bundle_match(const TVec<fir::ValueR> & /*values*/,
                     BundleCtx & /*ctx*/) {
  return false;
}
bool zext_legal(const PlanNode &n, LaneSpace space, u32 vf,
                const Features & /*features*/) {
  return space == LaneSpace::Bundle ||
         mask_width_ok(n.type->get_bitwidth(), vf);
}
bool widen_legal(const PlanNode & /*n*/, LaneSpace space, u32 /*vf*/,
                 const Features & /*features*/) {
  return space == LaneSpace::LoopIter;
}
bool itrunc_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->result_type();
  const auto src = i->args[0].get_type();
  if (!ty->is_int() || !src->is_int() || is_i1(ty) || is_i1(src) ||
      !is_vector_shape(shape_of(c, i->args[0])) ||
      (ty->get_bitwidth() != 8 && ty->get_bitwidth() != 16 &&
       ty->get_bitwidth() != 32) ||
      (src->get_bitwidth() != 16 && src->get_bitwidth() != 32 &&
       src->get_bitwidth() != 64) ||
      ty->get_bitwidth() >= src->get_bitwidth()) {
    return false;
  }
  n.kind = NodeKind::ITrunc;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.op_type = src;
  loop_children(c, n, std::array{i->args[0]});
  return true;
}
bool itrunc_legal(const PlanNode & /*n*/, LaneSpace space, u32 /*vf*/,
                  const Features & /*features*/) {
  // a truncation of a bundle would have no partial vector type to live in
  // (the planner of the loops checks the shapes), and a truncation to i1
  // would make a mask
  return space == LaneSpace::LoopIter;
}

bool conversion_legal(const PlanNode &n, LaneSpace /*space*/, u32 /*vf*/,
                      const Features & /*features*/) {
  // the backend only has scalar fp <-> int / ext / trunc conversions
  switch (static_cast<fir::ConversionSubType>(n.sub)) {
    case fir::ConversionSubType::BitCast:
    case fir::ConversionSubType::PtrToInt:
    case fir::ConversionSubType::IntToPtr:
      return true;
    default:
      return false;
  }
}

bool intrin_match_loop(LoopCtx &c, fir::Instr i, PlanNode &n) {
  const auto ty = i->result_type();
  size_t n_operands = 0;
  switch (static_cast<fir::IntrinsicSubType>(i->subtype)) {
    case fir::IntrinsicSubType::Abs:
      n_operands = ty->is_int() ? 1 : 0;
      break;
    case fir::IntrinsicSubType::FAbs:
      n_operands = ty->is_float() ? 1 : 0;
      break;
    case fir::IntrinsicSubType::UMin:
    case fir::IntrinsicSubType::UMax:
    case fir::IntrinsicSubType::SMin:
    case fir::IntrinsicSubType::SMax:
      n_operands = ty->is_int() ? 2 : 0;
      break;
    case fir::IntrinsicSubType::FMin:
    case fir::IntrinsicSubType::FMax:
      n_operands = ty->is_float() ? 2 : 0;
      break;
    default:
      break;
  }
  // the backend has no byte and word abs
  if (n_operands == 0 || (ty->get_bitwidth() < 32 &&
                          static_cast<fir::IntrinsicSubType>(i->subtype) ==
                              fir::IntrinsicSubType::Abs)) {
    return false;
  }
  for (size_t k = 0; k < n_operands; k++) {
    if (!vector_operand_ok(c, i->args[k])) {
      return false;
    }
  }
  n.kind = NodeKind::Intrin;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.sub = i->subtype;
  if (n_operands == 1) {
    loop_children(c, n, std::array{i->args[0]});
  } else {
    loop_children(c, n, std::array{i->args[0], i->args[1]});
  }
  return true;
}
bool intrin_legal(const PlanNode &n, LaneSpace /*space*/, u32 /*vf*/,
                  const Features &features) {
  switch (static_cast<fir::IntrinsicSubType>(n.sub)) {
    case fir::IntrinsicSubType::INVALID:
    case fir::IntrinsicSubType::VA_start:
    case fir::IntrinsicSubType::VA_end:
    case fir::IntrinsicSubType::IsConstant:
    case fir::IntrinsicSubType::Memset:
    case fir::IntrinsicSubType::Memcpy:
    case fir::IntrinsicSubType::Memmove:
    case fir::IntrinsicSubType::Memcmp:
    case fir::IntrinsicSubType::Gather:
    case fir::IntrinsicSubType::MaskedLoad:
    case fir::IntrinsicSubType::MaskedStore:
    case fir::IntrinsicSubType::Bswap:
    case fir::IntrinsicSubType::InlineAsm:
    case fir::IntrinsicSubType::InlineAsmPure:
    case fir::IntrinsicSubType::TlsAddress:
      return false;
    // no vector forms in the backend
    case fir::IntrinsicSubType::PopCnt:
    case fir::IntrinsicSubType::CTLZ:
    case fir::IntrinsicSubType::CTTZ:
    case fir::IntrinsicSubType::FMinimum:
    case fir::IntrinsicSubType::FMaximum:
    case fir::IntrinsicSubType::Fma:
    case fir::IntrinsicSubType::Clmul:
    case fir::IntrinsicSubType::PackSS:
    case fir::IntrinsicSubType::PackUS:
    case fir::IntrinsicSubType::PMulHW:
    case fir::IntrinsicSubType::PMaddWD:
    case fir::IntrinsicSubType::PMaddUBSW:
    case fir::IntrinsicSubType::PermD:
    case fir::IntrinsicSubType::MoveMask:
      return false;
    case fir::IntrinsicSubType::UMin:
    case fir::IntrinsicSubType::UMax:
    case fir::IntrinsicSubType::SMin:
    case fir::IntrinsicSubType::SMax:
      // 64 bit lanes need avx512vl
      return n.type->get_bitwidth() != 64 ||
             (features.avx512f && features.avx512vl);
    case fir::IntrinsicSubType::FMin:
    case fir::IntrinsicSubType::FMax:
    case fir::IntrinsicSubType::Abs:
    case fir::IntrinsicSubType::FAbs:
    case fir::IntrinsicSubType::FRound:
    case fir::IntrinsicSubType::FCeil:
    case fir::IntrinsicSubType::FFloor:
    case fir::IntrinsicSubType::FTrunc:
      return true;
  }
}

i64 unit_cost(const PlanNode & /*n*/, u32 /*vf*/,
              const Features & /*features*/) {
  return 1;
}
i64 call_cost(const PlanNode & /*n*/, u32 /*vf*/,
              const Features & /*features*/) {
  return 4;
}
i64 extract_cost(const PlanNode & /*n*/, u32 /*vf*/,
                 const Features & /*features*/) {
  return 0;
}

const Recipe Recipes[] = {
    {.type = fir::InstrType::StoreInstr,
     .kind = NodeKind::Store,
     .name = "store",
     .match_bundle = store_match_bundle,
     .match_loop = store_match_loop,
     .init = store_init,
     .n_args = [](const TVec<fir::ValueR> &) -> size_t { return 2; },
     .legal = always_legal,
     .cost = unit_cost},
    {.type = fir::InstrType::BinaryInstr,
     .kind = NodeKind::Binary,
     .name = "binary",
     .match_bundle = binary_match_bundle,
     .match_loop = binary_match_loop,
     .init = init_flex,
     .n_args = [](const TVec<fir::ValueR> &) -> size_t { return 2; },
     .legal = binary_legal,
     .cost = binary_cost},
    {.type = fir::InstrType::LoadInstr,
     .kind = NodeKind::Load,
     .name = "load",
     .match_bundle = load_match_bundle,
     .match_loop = load_match_loop,
     .init = load_init,
     .n_args = n_args_1,
     .legal = load_legal,
     .cost = load_cost},
    {.type = fir::InstrType::Intrinsic,
     .kind = NodeKind::Intrin,
     .name = "intrinsic",
     .match_bundle = match_same_instr_back,
     .match_loop = intrin_match_loop,
     .init = init_back,
     .n_args = n_args_instr,
     .legal = intrin_legal,
     .cost = unit_cost},
    {.type = fir::InstrType::ITrunc,
     .kind = NodeKind::ITrunc,
     .name = "itrunc",
     .match_bundle = match_same_instr_back,
     .match_loop = itrunc_match_loop,
     .init = init_back,
     .n_args = n_args_1,
     .legal = itrunc_legal,
     .cost = unit_cost},
    {.type = fir::InstrType::ZExt,
     .kind = NodeKind::Zext,
     .name = "zext",
     .match_bundle = zext_match,
     .match_loop = zext_match_loop,
     .init = init_flex,
     .n_args = n_args_1,
     .legal = zext_legal,
     .cost = unit_cost},
    {.type = fir::InstrType::SExt,
     .kind = NodeKind::Widen,
     .name = "widen",
     .match_bundle = no_bundle_match,
     .match_loop = sext_match_loop,
     .init = init_flex,
     .n_args = n_args_1,
     .legal = widen_legal,
     .cost = unit_cost},
    {.type = fir::InstrType::UnaryInstr,
     .kind = NodeKind::Unary,
     .name = "unary",
     .match_bundle = unary_match,
     .match_loop = unary_match_loop,
     .init = init_back,
     .n_args = n_args_1,
     .legal = always_legal,
     .cost = unit_cost},
    {.type = fir::InstrType::ExtractElement,
     .kind = NodeKind::Extract,
     .name = "extract",
     .match_bundle = extract_match,
     .match_loop = nullptr,
     .init = extract_init,
     .n_args = n_args_0,
     .legal = always_legal,
     .cost = extract_cost},
    {.type = fir::InstrType::CallInstr,
     .kind = NodeKind::Call,
     .name = "call",
     .match_bundle = call_match,
     .match_loop = nullptr,
     .init = call_init,
     .n_args = n_args_instr,
     .legal = always_legal,
     .cost = call_cost},
    {.type = fir::InstrType::Conversion,
     .kind = NodeKind::Conversion,
     .name = "conversion",
     .match_bundle = match_same_instr,
     .match_loop = nullptr,
     .init = init_flex,
     .n_args = n_args_1,
     .legal = conversion_legal,
     .cost = unit_cost},
    {.type = fir::InstrType::SelectInstr,
     .kind = NodeKind::Select,
     .name = "select",
     .match_bundle = select_match_bundle,
     .match_loop = select_match_loop,
     .init = init_flex,
     .n_args = [](const TVec<fir::ValueR> &) -> size_t { return 3; },
     .legal = select_legal,
     .cost = unit_cost},
    {.type = fir::InstrType::ICmp,
     .kind = NodeKind::Cmp,
     .name = "cmp",
     .match_bundle = cmp_match_bundle,
     .match_loop = cmp_match_loop,
     .init = cmp_init,
     .n_args = n_args_2,
     .legal = cmp_legal,
     .cost = cmp_cost},
    {.type = fir::InstrType::FCmp,
     .kind = NodeKind::Cmp,
     .name = "cmp",
     .match_bundle = cmp_match_bundle,
     .match_loop = cmp_match_loop,
     .init = cmp_init,
     .n_args = n_args_2,
     .legal = cmp_legal,
     .cost = cmp_cost},
};
}  // namespace

void init_back(PlanNode &n, const TVec<fir::ValueR> &values, const InitCtx &c) {
  n.n_lanes = values.size();
  n.insert_loc = last_lane(values, c);
  auto lane = values.back().as_instr();
  n.type = lane->result_type();
  n.sub = lane->subtype;
}

void init_build_root(PlanNode &n, const TVec<fir::ValueR> &values,
                     fir::TypeR elem, const InitCtx &c) {
  n.n_lanes = values.size();
  n.insert_loc = last_lane(values, c);
  n.type = elem;
}

// any operand where all lanes are the same, not a recipe
bool broadcast_match(const TVec<fir::ValueR> &values) {
  auto base_v = values.back();
  if (base_v.is_constant()) {
    return false;
  }
  auto ty = base_v.get_type();
  if (!ty->is_int() && !ty->is_ptr() && !ty->is_float()) {
    return false;
  }
  // a mask is not broadcast
  if (is_i1(ty)) {
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
    if (i_v.get_type() != base_v->get_type() || is_i1(i_v.get_type())) {
      return false;
    }
  }
  return true;
}

bool vector_type_ok(fir::TypeR elem, u32 lanes, const Features &features) {
  // vectors of pointers are vectors of 64 bit integers
  const u32 bits = elem->is_ptr() ? 64 : elem->get_bitwidth();
  const u32 total = bits * lanes;
  if (total > (features.avx512f ? 512U : 256U)) {
    return false;
  }
  if (elem->is_ptr()) {
    return total >= 128;
  }
  if (elem->is_float()) {
    // there is also a two lane f32 vector (8 byte)
    return (bits == 32 && lanes >= 2) || (bits == 64 && lanes >= 2);
  }
  return elem->is_int() &&
         (bits == 8 || bits == 16 || bits == 32 || bits == 64) && total >= 128;
}

// Operands that no recipe matches are built from their scalars: a zero vector
// with every lane inserted. The scalars stay where they are.
bool gather_legal(const TVec<fir::ValueR> &values,
                  const Features & /*features*/) {
  const auto ty = values.back().get_type();
  if ((!ty->is_int() && !ty->is_float()) || is_i1(ty)) {
    return false;
  }
  const u32 bits = ty->get_bitwidth();
  const u32 total = bits * static_cast<u32>(values.size());
  if ((bits != 32 && bits != 64) || (total != 128 && total != 256)) {
    return false;
  }
  for (auto v : values) {
    if (v.get_type() != ty || v.is_poison()) {
      return false;
    }
  }
  return true;
}

void gather_init(PlanNode &n, const TVec<fir::ValueR> &values,
                 const InitCtx &c) {
  n.n_lanes = static_cast<u32>(values.size());
  n.lanes = values;
  n.type = values.back().get_type();
  n.shape = LaneShape::Varying;
  // behind the last lane that is defined in the seed block
  n.insert_loc = last_lane(values, c);
  n.after = false;
  for (auto v : values) {
    n.after |= lane_instr(v) && v.as_instr() == n.insert_loc;
  }
}

fir::ValueR zero_vector(fir::Context &ctx, fir::TypeR elem, u32 lanes) {
  IRVec<fir::ConstantValueR> members;
  for (u32 i = 0; i < lanes; i++) {
    members.push_back(elem->is_int()
                          ? ctx->get_constant_int(0, elem->get_bitwidth())
                          : (elem->get_bitwidth() == 32
                                 ? ctx->get_constant_value(0.0F, elem)
                                 : ctx->get_constant_value(0.0, elem)));
  }
  return fir::ValueR{ctx->get_constant_value(std::move(members),
                                             ctx->get_vec_type(elem, lanes))};
}

const Recipe *find_recipe(fir::InstrType type) {
  for (const auto &r : Recipes) {
    if (r.type == type) {
      return &r;
    }
  }
  return nullptr;
}

const Recipe *recipe_of(NodeKind kind) {
  for (const auto &r : Recipes) {
    if (r.kind == kind) {
      return &r;
    }
  }
  return nullptr;
}

std::optional<fir::ReduceInstrSubType> halving_op_for(
    fir::Instr root, u32 lanes, const Features &features) {
  const auto ty = root->result_type();
  const u32 bits = ty->get_bitwidth();
  // 128 and 256 bit vectors of 32 or 64 bit lanes
  if ((bits != 32 && bits != 64) || lanes * bits < 128 || lanes * bits > 256) {
    return std::nullopt;
  }
  using R = fir::ReduceInstrSubType;
  if (root->is(fir::InstrType::BinaryInstr)) {
    switch (static_cast<BinSub>(root->subtype)) {
      case BinSub::IntAdd:
        return R::Add;
      case BinSub::And:
        return R::And;
      case BinSub::Or:
        return R::Or;
      case BinSub::Xor:
        return R::Xor;
      default:
        return std::nullopt;
    }
  }
  if (!root->is(fir::InstrType::Intrinsic)) {
    return std::nullopt;
  }
  const bool vl64 = bits != 64 || (features.avx512f && features.avx512vl);
  switch (static_cast<fir::IntrinsicSubType>(root->subtype)) {
    case fir::IntrinsicSubType::SMin:
      return vl64 ? std::optional{R::SMin} : std::nullopt;
    case fir::IntrinsicSubType::SMax:
      return vl64 ? std::optional{R::SMax} : std::nullopt;
    case fir::IntrinsicSubType::UMin:
      return vl64 ? std::optional{R::UMin} : std::nullopt;
    case fir::IntrinsicSubType::UMax:
      return vl64 ? std::optional{R::UMax} : std::nullopt;
    case fir::IntrinsicSubType::FMin:
      return R::FMin;
    case fir::IntrinsicSubType::FMax:
      return R::FMax;
    default:
      return std::nullopt;
  }
}

std::optional<fir::ReduceInstrSubType> reduce_op_for(fir::Instr root, u32 lanes,
                                                     const Features &features) {
  if (auto op = halving_op_for(root, lanes, features)) {
    return op;
  }
  if (!root->is(fir::InstrType::BinaryInstr)) {
    return std::nullopt;
  }
  switch (static_cast<BinSub>(root->subtype)) {
    case BinSub::IntAdd:
    case BinSub::FloatAdd:
      return fir::ReduceInstrSubType::Add;
    case BinSub::FloatMul:
      return fir::ReduceInstrSubType::Mul;
    default:
      return std::nullopt;
  }
}

bool node_legal(const PlanNode &n, LaneSpace space, u32 vf,
                const Features &features) {
  const auto *r = recipe_of(n.kind);
  return r == nullptr || r->legal(n, space, vf, features);
}

i64 node_vec_cost(const PlanNode &n, u32 vf, const Features &features) {
  switch (n.kind) {
    case NodeKind::Broadcast:
      return 1;
    case NodeKind::Gather:
      // a lane insert is two operations
      return 2 * static_cast<i64>(vf);
    case NodeKind::HorizRed: {
      const auto root = n.value.as_instr();
      const bool binary = root->is(fir::InstrType::BinaryInstr);
      if (halving_op_for(root, vf, features)) {
        const u32 lanes128 = 128 / n.type->get_bitwidth();
        const i64 steps =
            std::countr_zero(std::min(vf, lanes128)) + (vf > lanes128 ? 1 : 0);
        return 2 * steps + 1;
      }
      if (reduce_op_for(root, vf, features)) {
        return std::countr_zero(std::bit_ceil(vf));
      }
      // an extract costs about two operations
      const u32 lanes128 =
          binary ? std::min(vf, 128 / n.type->get_bitwidth()) : vf;
      return 3 * static_cast<i64>(lanes128) - 1 +
             (binary && vf > lanes128 ? 2 : 0);
    }
    case NodeKind::Constant:
    case NodeKind::Induction:
    case NodeKind::Reduction:
    case NodeKind::BuildRoot:
      return 0;
    case NodeKind::Scalar:
    case NodeKind::Phi:
      return 1;
    default:
      return recipe_of(n.kind)->cost(n, vf, features);
  }
}

i64 node_scalar_cost(const PlanNode &n) {
  const auto lanes = static_cast<i64>(n.n_lanes);
  switch (n.kind) {
    case NodeKind::Broadcast:
    case NodeKind::Gather:
    case NodeKind::Constant:
    case NodeKind::Induction:
    case NodeKind::Reduction:
    case NodeKind::Scalar:
      return 0;
    case NodeKind::HorizRed:
      // the chain of lanes - 1 operations
      return lanes - 1;
    case NodeKind::BuildRoot:
      return n.scalar_ops;
    case NodeKind::Call:
      return lanes * 4;
    case NodeKind::Store:
      // the store and the address it computes
      return n.scalar_ops != 0 ? static_cast<i64>(n.scalar_ops) : lanes * 2;
    default:
      return lanes;
  }
}

i64 bundle_benefit(const Plan &plan, u32 id, const Features &features) {
  const auto &n = plan.nodes[id];
  i64 benefit = node_scalar_cost(n) - node_vec_cost(n, n.n_lanes, features);
  for (size_t i = 0; i < n.children.size(); i++) {
    // the address of a store is the address of its first lane
    if (n.kind == NodeKind::Store && i != 0) {
      break;
    }
    benefit += bundle_benefit(plan, n.children[i], features);
  }
  return benefit;
}

fir::ValueR emit_node(fir::Context &ctx, fir::Builder &bb, const PlanNode &n,
                      u32 vf, std::span<const fir::ValueR> ops) {
  switch (n.kind) {
    case NodeKind::Binary:
      return bb.build_binary_op(ops[0], ops[1], static_cast<BinSub>(n.sub));
    case NodeKind::Zext:
      return bb.build_zext(ops[0], ctx->get_vec_type(n.type, vf));
    case NodeKind::Widen:
      return n.sub == 1 ? bb.build_sext(ops[0], ctx->get_vec_type(n.type, vf))
                        : bb.build_zext(ops[0], ctx->get_vec_type(n.type, vf));
    case NodeKind::ITrunc:
      return bb.build_itrunc(ops[0], ctx->get_vec_type(n.type, vf));
    case NodeKind::Unary:
      return bb.build_unary_op(ops[0],
                               static_cast<fir::UnaryInstrSubType>(n.sub));
    case NodeKind::Intrin:
      switch (static_cast<fir::IntrinsicSubType>(n.sub)) {
        default:
          TODO("UNREACH");
        case fir::IntrinsicSubType::UMin:
        case fir::IntrinsicSubType::UMax:
        case fir::IntrinsicSubType::SMin:
        case fir::IntrinsicSubType::SMax:
          return bb.build_intrinsic(ops[0], ops[1],
                                    static_cast<fir::IntrinsicSubType>(n.sub));
        case fir::IntrinsicSubType::FMin:
          return bb.build_intrinsic(ops[1], ops[0],
                                    fir::IntrinsicSubType::FMin);
        case fir::IntrinsicSubType::FMax:
          return bb.build_intrinsic(ops[1], ops[0],
                                    fir::IntrinsicSubType::FMax);
        case fir::IntrinsicSubType::FRound:
          return bb.build_intrinsic(ops[0], fir::IntrinsicSubType::FRound);
        case fir::IntrinsicSubType::FCeil:
          return bb.build_intrinsic(ops[0], fir::IntrinsicSubType::FCeil);
        case fir::IntrinsicSubType::FFloor:
          return bb.build_intrinsic(ops[0], fir::IntrinsicSubType::FFloor);
        case fir::IntrinsicSubType::Abs:
          return bb.build_abs(ops[0]);
        case fir::IntrinsicSubType::FAbs:
          return bb.build_fabs(ops[0]);
      }
    case NodeKind::Conversion:
      return bb.build_conversion_op(ops[0], ctx->get_vec_type(n.type, vf),
                                    static_cast<fir::ConversionSubType>(n.sub));
    case NodeKind::Cmp:
      if (n.op_type->is_float()) {
        return bb.build_float_cmp(ops[0], ops[1],
                                  static_cast<fir::FCmpInstrSubType>(n.sub));
      }
      return bb.build_int_cmp(ops[0], ops[1],
                              static_cast<fir::ICmpInstrSubType>(n.sub));
    case NodeKind::Select:
      // operands are cond, a, b
      return bb.build_select(ctx->get_vec_type(n.type, vf), ops[0], ops[1],
                             ops[2]);
    case NodeKind::Load:
      return bb.build_load(ctx->get_vec_type(n.type, vf), ops[0], false, false);
    case NodeKind::Store:
      return bb.build_store(ops[0], ops[1], false, false);
    default:
      UNREACH();
  }
}

void node_dump(const Plan &plan, u32 id, u32 indent) {
  static constexpr const char *names[] = {
      "broadcast",  "gather", "horiz_red", "binary",    "zext",      "itrunc",
      "call",       "unary",  "extract",   "store",     "intrin",    "constant",
      "conversion", "select", "load",      "induction", "reduction", "scalar",
      "cmp",        "phi",    "widen"};
  static constexpr const char *shapes[] = {"uniform", "consecutive", "varying",
                                           "strided", "recurrence"};
  const auto &n = plan.nodes[id];
  fmt::println("{:{}}{}<{}> x{}", "", indent * 2,
               names[static_cast<u32>(n.kind)],
               shapes[static_cast<u32>(n.shape)], n.n_lanes);
  for (auto c : n.children) {
    node_dump(plan, c, indent + 1);
  }
}

bool elem_type_ok(fir::TypeR ty) { return ty->is_int() || ty->is_float(); }

bool is_vector_shape(LaneShape s) {
  return s == LaneShape::Varying || s == LaneShape::Recurrence;
}

u32 shape_node(const LoopCtx &c, fir::ValueR v) {
  auto it = c.plan.loop.node_of.find(v);
  return it == c.plan.loop.node_of.end() ? ~0U : it->second;
}

LaneShape shape_of(const LoopCtx &c, fir::ValueR v) {
  const u32 n = shape_node(c, v);
  return n == ~0U ? LaneShape::Uniform : c.plan.nodes[n].shape;
}

}  // namespace foptim::optim::vec
