#include "slp_vectorizer.hpp"

#include <algorithm>
#include <bit>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/block_mem_access.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/helper/scev_expander.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

namespace {
using LoopPlan = SLPVectorizer::LoopPlan;
using LoopNode = SLPVectorizer::LoopNode;
using LoopNodeKind = SLPVectorizer::LoopNodeKind;
using LaneShape = SLPVectorizer::LaneShape;
using Features = conf::Target::Features;
using BinSub = fir::BinaryInstrSubType;

bool is_vector_shape(LaneShape s) {
  return s == LaneShape::Varying || s == LaneShape::Recurrence;
}

// One recipe per widenable instruction type; a new one is a new table entry.
struct MatchCtx {
  LoopPlan &plan;
  ScalarEvo &evo;
  const fir::Context &ctx;
};

bool elem_type_ok(fir::TypeR ty) { return ty->is_int() || ty->is_float(); }

u32 shape_node(MatchCtx &c, fir::ValueR v) {
  auto it = c.plan.node_of.find(v);
  return it == c.plan.node_of.end() ? ~0U : it->second;
}

LaneShape shape_of(MatchCtx &c, fir::ValueR v) {
  const u32 n = shape_node(c, v);
  return n == ~0U ? LaneShape::Uniform : c.plan.nodes[n].shape;
}

// Strided operands would need an iota vector
bool vector_operand_ok(MatchCtx &c, fir::ValueR v) {
  return shape_of(c, v) != LaneShape::Strided;
}

struct LoopRecipe {
  fir::InstrType type;
  const char *name;
  bool (*match)(MatchCtx &, fir::Instr, LoopNode &);
  bool (*legal)(const LoopNode &, const LoopPlan &, u32 vf, const Features &);
  i64 (*cost)(const LoopNode &, const LoopPlan &, u32 vf, const Features &);
};

// unit stride access means the address steps by the element size each iteration
bool unit_stride(MatchCtx &c, fir::ValueR ptr, u32 size) {
  const auto r = c.evo.get(ptr);
  return c.evo.is_addrec(r) && c.evo.is_const(c.evo.expr(r).b) &&
         c.evo.expr(c.evo.expr(r).b).cval == static_cast<i128>(size);
}

bool load_match(MatchCtx &c, fir::Instr i, LoopNode &n) {
  const auto ty = i->get_type();
  if (!elem_type_ok(ty) || !unit_stride(c, i->args[0], ty->get_size())) {
    return false;
  }
  const u32 p = shape_node(c, i->args[0]);
  if (p == ~0U) {
    return false;
  }
  n.kind = LoopNodeKind::Load;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.ops = {p};
  return true;
}

bool store_match(MatchCtx &c, fir::Instr i, LoopNode &n) {
  const auto ty = i->args[1].get_type();
  if (!elem_type_ok(ty) || !unit_stride(c, i->args[0], ty->get_size()) ||
      !vector_operand_ok(c, i->args[1])) {
    return false;
  }
  const u32 p = shape_node(c, i->args[0]);
  if (p == ~0U) {
    return false;
  }
  n.kind = LoopNodeKind::Store;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.ops = {p, shape_node(c, i->args[1])};
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

bool binary_match(MatchCtx &c, fir::Instr i, LoopNode &n) {
  const auto ty = i->get_type();
  if (!elem_type_ok(ty) || !binary_sub_ok(static_cast<BinSub>(i->subtype)) ||
      !vector_operand_ok(c, i->args[0]) || !vector_operand_ok(c, i->args[1])) {
    return false;
  }
  n.kind = LoopNodeKind::Binary;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.sub = i->subtype;
  n.ops = {shape_node(c, i->args[0]), shape_node(c, i->args[1])};
  return true;
}

bool intrinsic_match(MatchCtx &c, fir::Instr i, LoopNode &n) {
  const auto ty = i->get_type();
  const bool abs = i->is(fir::IntrinsicSubType::Abs) && ty->is_int();
  const bool fabs = i->is(fir::IntrinsicSubType::FAbs) && ty->is_float();
  if (!(abs || fabs) || !vector_operand_ok(c, i->args[0])) {
    return false;
  }
  n.kind = LoopNodeKind::Intrinsic;
  n.shape = LaneShape::Varying;
  n.type = ty;
  n.sub = i->subtype;
  n.ops = {shape_node(c, i->args[0])};
  return true;
}

bool always_legal(const LoopNode &, const LoopPlan &, u32, const Features &) {
  return true;
}

bool binary_legal(const LoopNode &n, const LoopPlan &, u32 /*vf*/,
                  const Features &features) {
  const auto sub = static_cast<BinSub>(n.sub);
  // no 64 bit vector multiply before avx512dq
  if (sub == BinSub::IntMul && n.type->get_bitwidth() == 64 &&
      !features.avx512dq) {
    return false;
  }
  return true;
}

i64 unit_cost(const LoopNode &, const LoopPlan &, u32, const Features &) {
  return 1;
}
i64 binary_cost(const LoopNode &n, const LoopPlan &, u32, const Features &) {
  const auto sub = static_cast<BinSub>(n.sub);
  if (sub == BinSub::IntMul && n.type->get_bitwidth() == 32) {
    return 2;
  }
  if (sub == BinSub::FloatDiv) {
    return 6;
  }
  return 1;
}

constexpr LoopRecipe LoopRecipes[] = {
    {fir::InstrType::LoadInstr, "load", load_match, always_legal, unit_cost},
    {fir::InstrType::StoreInstr, "store", store_match, always_legal,
     unit_cost},
    {fir::InstrType::BinaryInstr, "binary", binary_match, binary_legal,
     binary_cost},
    {fir::InstrType::Intrinsic, "intrinsic", intrinsic_match, always_legal,
     unit_cost},
};

const LoopRecipe *find_loop_recipe(fir::InstrType type) {
  for (const auto &r : LoopRecipes) {
    if (r.type == type) {
      return &r;
    }
  }
  return nullptr;
}

bool is_reduction_sub(BinSub sub, bool assoc_float) {
  switch (sub) {
  case BinSub::IntAdd:
  case BinSub::IntMul:
    return true;
  case BinSub::FloatAdd:
  case BinSub::FloatMul:
    return assoc_float;
  default:
    return false;
  }
}

fir::ValueR reduction_identity(fir::Context &ctx, fir::TypeR ty, BinSub sub) {
  const bool mul = sub == BinSub::IntMul || sub == BinSub::FloatMul;
  if (ty->is_int()) {
    return fir::ValueR{ctx->get_constant_int(mul ? 1 : 0, ty->get_bitwidth())};
  }
  if (ty->get_bitwidth() == 32) {
    return fir::ValueR{ctx->get_constant_value(mul ? 1.0F : 0.0F, ty)};
  }
  return fir::ValueR{ctx->get_constant_value(mul ? 1.0 : 0.0, ty)};
}
} // namespace

u32 SLPVectorizer::max_vector_bits(const Features &features) {
  return features.avx512f ? 512 : 256;
}

bool SLPVectorizer::loop_legal(const LoopPlan &plan, u32 vf, const Features &features) {
  // backend support is only verified for 32 and 64 bit elements
  if (plan.elem_bits != 32 && plan.elem_bits != 64) {
    return false;
  }
  const u32 bits = vf * plan.elem_bits;
  if (vf < 2 || !std::has_single_bit(vf) || bits < 128 ||
      bits > max_vector_bits(features)) {
    return false;
  }
  for (const auto &n : plan.nodes) {
    if (!is_vector_shape(n.shape)) {
      continue;
    }
    if (n.kind == LoopNodeKind::Reduction) {
      // the backend has no horizontal multiply (and no 512 bit integer add)
      const auto sub = static_cast<BinSub>(n.sub);
      const bool add = sub == BinSub::IntAdd || sub == BinSub::FloatAdd;
      if (!add || (n.type->is_int() && bits > 256)) {
        return false;
      }
      continue;
    }
    const LoopRecipe *r = nullptr;
    switch (n.kind) {
    case LoopNodeKind::Load:
      r = find_loop_recipe(fir::InstrType::LoadInstr);
      break;
    case LoopNodeKind::Store:
      r = find_loop_recipe(fir::InstrType::StoreInstr);
      break;
    case LoopNodeKind::Binary:
      r = find_loop_recipe(fir::InstrType::BinaryInstr);
      break;
    case LoopNodeKind::Intrinsic:
      r = find_loop_recipe(fir::InstrType::Intrinsic);
      break;
    default:
      break;
    }
    if (r != nullptr && !r->legal(n, plan, vf, features)) {
      return false;
    }
  }
  return true;
}

i64 SLPVectorizer::loop_cost(const LoopPlan &plan, u32 vf,
                                const Features &features, u32 assumed_trip,
                                i64 &scalar_total) {
  const i64 trip =
      plan.const_trip ? static_cast<i64>(*plan.const_trip) : assumed_trip;
  const i64 s = plan.scalar_cost;
  scalar_total = trip * s;
  i64 body = 0;
  for (const auto &n : plan.nodes) {
    switch (n.kind) {
    case LoopNodeKind::Load:
      body += unit_cost(n, plan, vf, features);
      break;
    case LoopNodeKind::Store:
      body += unit_cost(n, plan, vf, features);
      break;
    case LoopNodeKind::Binary:
      body += binary_cost(n, plan, vf, features);
      break;
    case LoopNodeKind::Intrinsic:
      body += unit_cost(n, plan, vf, features);
      break;
    case LoopNodeKind::Scalar:
      body += 1;
      break;
    default:
      break;
    }
  }
  body += 3;
  const i64 vtrip = trip / vf * vf;
  const i64 rest = trip - vtrip;
  i64 overhead = 8 + plan.n_reductions * 2 * std::countr_zero(vf);
  return overhead + (vtrip / vf) * body + rest * s;
}

bool SLPVectorizer::build_loop_plan(fir::Context &ctx, CFG &cfg, Dominators &dom,
                                LoopInfo &loop, ScalarEvo &evo, LoopPlan &plan) {
  if (loop.body_nodes.size() != 1 || loop.tails.size() != 1 ||
      loop.head != loop.tails[0]) {
    return false;
  }
  auto hdr = cfg.bbrs[loop.head].bb;
  auto term = hdr->get_terminator();
  if (!term->is(fir::InstrType::CondBranchInstr) || term->bbs.size() != 2) {
    return false;
  }
  if (term->bbs[0].bb == hdr && term->bbs[1].bb == hdr) {
    return false;
  }
  plan.header = hdr;
  plan.back_edge = term->bbs[0].bb == hdr ? 0 : 1;
  plan.exit_edge = 1 - plan.back_edge;
  const auto &back_args = term->bbs[plan.back_edge].args;
  const auto &exit_args = term->bbs[plan.exit_edge].args;

  // trip count and loop entry
  TripCountAnalysis tc;
  if (!tc.update(cfg, dom, loop, evo) || tc.exit_iter == InvalidSCEV ||
      tc.from_bb != hdr || !evo.is_expandable(tc.exit_iter)) {
    return false;
  }
  auto entry = evo.entry_block();
  if (!entry.is_valid()) {
    return false;
  }
  if (tc.cmp_rec != InvalidSCEV && (tc.cmp_signed || tc.cmp_unsigned)) {
    evo.assume_no_wrap(tc.cmp_rec, tc.cmp_signed);
  }
  {
    auto eterm = entry->get_terminator();
    u32 edges = 0;
    for (const auto &e : eterm->bbs) {
      edges += e.bb == hdr ? 1 : 0;
    }
    if (edges != 1) {
      return false;
    }
  }
  plan.trip = tc.exit_iter;
  if (tc.const_exit_iter) {
    plan.const_trip = *tc.const_exit_iter + 1;
  }
  const u32 trip_bw = evo.expr(tc.exit_iter).bw;

  MatchCtx mc{plan, evo, ctx};
  const bool assoc_float = ctx.config->optim.fltOpt.associative_math;

  // header arguments are induction variables or reductions
  bool have_counter = false;
  for (u32 k = 0; k < hdr->args.size(); k++) {
    fir::ValueR av{hdr->args[k]};
    const auto ty = av.get_type();
    if (!elem_type_ok(ty)) {
      return false;
    }
    LoopNode n;
    n.value = av;
    n.type = ty;
    n.arg_id = k;
    const auto r = evo.get(av);
    if (ty->is_int() && evo.is_const_addrec(r)) {
      n.kind = LoopNodeKind::Induction;
      n.shape = LaneShape::Strided;
      n.stride = static_cast<i64>(evo.expr(evo.expr(r).b).cval);
      if (n.stride == 1 && !have_counter && ty->get_bitwidth() == trip_bw) {
        have_counter = true;
        plan.counter = k;
      }
    } else {
      // a reduction is acc2 = acc op x and nothing else touches acc or acc2
      if (!config.reductions || !back_args[k].is_instr()) {
        return false;
      }
      auto next = back_args[k].as_instr();
      if (next->get_parent() != hdr || !next->is(fir::InstrType::BinaryInstr)) {
        return false;
      }
      const auto sub = static_cast<BinSub>(next->subtype);
      if (!is_reduction_sub(sub, assoc_float)) {
        return false;
      }
      const bool a0 = next->args[0] == av;
      const bool a1 = next->args[1] == av;
      if (a0 == a1) {
        return false;
      }
      for (const auto &u : av.as_bb_arg()->uses) {
        if (u.user != next) {
          return false;
        }
      }
      for (const auto &u : next->uses) {
        if (u.user != term || u.type != fir::UseType::BBArg) {
          return false;
        }
        // back edge: only into the argument it is the accumulator of
        if (u.argId == plan.back_edge && u.bbArgId != k) {
          return false;
        }
      }
      n.kind = LoopNodeKind::Reduction;
      n.shape = LaneShape::Recurrence;
      n.sub = next->subtype;
      plan.n_reductions++;
    }
    plan.node_of[av] = static_cast<u32>(plan.nodes.size());
    plan.nodes.push_back(std::move(n));
  }
  if (!have_counter) {
    return false;
  }

  // body
  BlockMemAccesses mem{hdr};
  if (mem.n_barriers() != 0) {
    return false;
  }
  const auto n_instrs = hdr->instructions.size();
  plan.scalar_cost = static_cast<u32>(n_instrs - 1);
  for (size_t idx = 0; idx + 1 < n_instrs; idx++) {
    auto instr = hdr->instructions[idx];
    fir::ValueR iv{instr};
    LoopNode n;
    n.value = iv;
    const LoopRecipe *recipe = find_loop_recipe(instr->instr_type);
    bool vector_operand = false;
    for (auto a : instr->args) {
      vector_operand |= is_vector_shape(shape_of(mc, a));
    }
    if (instr->is(fir::InstrType::LoadInstr) ||
        instr->is(fir::InstrType::StoreInstr)) {
      if (!recipe->match(mc, instr, n)) {
        return false;
      }
      auto a = mem.index_of(instr);
      if (!a) {
        return false;
      }
      plan.accesses.push_back(
          {.ptr = instr->args[0],
           .addr = evo.get(instr->args[0]),
           .size = n.type->get_size(),
           .is_store = instr->is(fir::InstrType::StoreInstr)});
      plan.n_vector_ops++;
    } else if (vector_operand) {
      if (recipe == nullptr || !recipe->match(mc, instr, n)) {
        return false;
      }
      plan.n_vector_ops++;
    } else {
      // computes the value of lane 0 (addresses, induction math), copied
      if (instr->has_pot_sideeffects() || instr->pot_reads_mem() ||
          instr->get_type()->is_vec() ||
          instr->get_type()->is_struct()) {
        return false;
      }
      n.kind = LoopNodeKind::Scalar;
      n.type = instr->get_type();
      n.shape = evo.is_invariant(evo.get(iv)) ? LaneShape::Uniform
                                              : LaneShape::Strided;
      for (auto a : instr->args) {
        const u32 o = shape_node(mc, a);
        n.ops.push_back(o);
      }
      plan.n_scalar_ops++;
    }
    plan.node_of[iv] = static_cast<u32>(plan.nodes.size());
    plan.nodes.push_back(std::move(n));
  }
  if (plan.n_vector_ops == 0 || plan.accesses.empty()) {
    return false;
  }

  // all vector values need the same element width
  for (const auto &n : plan.nodes) {
    if (!is_vector_shape(n.shape)) {
      continue;
    }
    const u32 bits = n.type->get_bitwidth();
    if (plan.elem_bits == 0) {
      plan.elem_bits = bits;
    } else if (plan.elem_bits != bits) {
      return false;
    }
  }

  // check the uses outside of the loop and of the vector values
  auto check_uses = [&](fir::ValueR v, const LoopNode *n) {
    const auto &uses = v.is_instr() ? v.as_instr()->uses : v.as_bb_arg()->uses;
    for (const auto &u : uses) {
      if (u.user->get_parent() != hdr) {
        return false;
      }
      if (u.user != term) {
        continue;
      }
      if (n != nullptr && is_vector_shape(n->shape) &&
          n->kind != LoopNodeKind::Reduction) {
        // vectors only leave as the next value of a reduction
        bool is_next = false;
        for (u32 k = 0; k < back_args.size(); k++) {
          is_next |= plan.nodes[k].kind == LoopNodeKind::Reduction &&
                     back_args[k] == v;
        }
        if (!is_next) {
          return false;
        }
      }
    }
    return true;
  };
  for (auto a : hdr->args) {
    if (!check_uses(fir::ValueR{a}, nullptr)) {
      return false;
    }
  }
  for (size_t idx = 0; idx + 1 < n_instrs; idx++) {
    auto instr = hdr->instructions[idx];
    if (!check_uses(fir::ValueR{instr}, &plan.nodes[plan.node_of.at(
                                            fir::ValueR{instr})])) {
      return false;
    }
  }
  // values passed to the code after the loop are loop invariant or the next
  // value of an induction variable or reduction
  for (const auto &e : exit_args) {
    bool ok = !(e.is_instr() && e.as_instr()->get_parent() == hdr) &&
              !(e.is_bb_arg() && e.as_bb_arg()->get_parent() == hdr);
    for (u32 k = 0; k < back_args.size() && !ok; k++) {
      const auto &kn = plan.nodes[k];
      ok = e == back_args[k] && (kn.kind == LoopNodeKind::Induction ||
                                 kn.kind == LoopNodeKind::Reduction);
    }
    if (!ok) {
      return false;
    }
  }
  for (u32 k = 0; k < back_args.size(); k++) {
    if (plan.nodes[k].kind == LoopNodeKind::Reduction &&
        !(back_args[k].is_instr() &&
          plan.node_of.contains(back_args[k]))) {
      return false;
    }
  }
  return true;
}

bool SLPVectorizer::emit_loop(fir::Context &ctx, fir::Function &func, CFG &cfg,
                          ScalarEvo &evo, LoopPlan &plan, u32 vf) {
  (void)func;
  (void)cfg;
  auto hdr = plan.header;
  auto term = hdr->get_terminator();
  auto entry = evo.entry_block();
  const u32 n_args = static_cast<u32>(hdr->args.size());
  const auto &back_args = term->bbs[plan.back_edge].args;
  auto exit_bb = term->bbs[plan.exit_edge].bb;
  const auto trip_bw = evo.expr(plan.trip).bw;

  auto add_arg = [&](fir::BasicBlock bb, fir::TypeR ty) {
    return fir::ValueR{bb.add_arg(ctx->storage.insert_bb_arg(bb, ty))};
  };
  auto vec_ty = [&](fir::TypeR elem) { return ctx->get_vec_type(elem, vf); };
  auto cint = [&](i128 v, u32 bw) {
    return fir::ValueR{ctx->get_constant_int(v, bw)};
  };

  fir::Builder blocks{hdr};
  auto vpre = blocks.append_bb();
  auto vloop = blocks.append_bb();
  auto vmid = blocks.append_bb();

  TVec<fir::ValueR> pre_args;
  TVec<fir::ValueR> vec_args;
  TVec<fir::ValueR> mid_args;
  for (u32 k = 0; k < n_args; k++) {
    const auto &n = plan.nodes[k];
    pre_args.push_back(add_arg(vpre, n.type));
    const auto ty = n.kind == LoopNodeKind::Reduction ? vec_ty(n.type) : n.type;
    vec_args.push_back(add_arg(vloop, ty));
    mid_args.push_back(add_arg(vmid, ty));
  }

  // header executions, computed in the entry block
  fir::ValueR total_trip;
  {
    SCEVExpander ex{ctx, evo, entry->get_terminator()};
    fir::Builder b{entry->get_terminator()};
    total_trip = b.build_int_add(ex.expand(plan.trip), cint(1, trip_bw));
  }
  entry->get_terminator().replace_bb(hdr, vpre, true);

  // pre block, vtrip is the trip count rounded down to a multiple of vf
  fir::ValueR vtrip;
  fir::ValueR end_counter;
  {
    fir::Builder b{vpre};
    b.at_end(vpre);
    vtrip = b.build_binary_op(total_trip,
                              cint(~static_cast<i128>(vf - 1), trip_bw),
                              BinSub::And);
    end_counter = b.build_int_add(pre_args[plan.counter], vtrip);
    auto none = b.build_int_cmp(vtrip, cint(0, trip_bw),
                                fir::ICmpInstrSubType::EQ);
    TVec<fir::ValueR> vec_init;
    for (u32 k = 0; k < n_args; k++) {
      const auto &n = plan.nodes[k];
      if (n.kind == LoopNodeKind::Reduction) {
        auto id = reduction_identity(ctx, n.type, static_cast<BinSub>(n.sub));
        vec_init.push_back(b.build_vbroadcast(id, vec_ty(n.type)));
      } else {
        vec_init.push_back(pre_args[k]);
      }
    }
    auto br = b.build_cond_branch(none, hdr, vloop);
    for (u32 k = 0; k < n_args; k++) {
      br.add_bb_arg(0, pre_args[k]);
      br.add_bb_arg(1, vec_init[k]);
    }
  }

  // vector loop
  TVec<fir::ValueR> next_vals;
  {
    fir::Builder b{vloop};
    b.at_end(vloop);
    TMap<fir::ValueR, fir::ValueR> vm;
    TMap<fir::ValueR, fir::ValueR> broadcasts;
    for (u32 k = 0; k < n_args; k++) {
      vm[plan.nodes[k].value] = vec_args[k];
    }
    auto operand = [&](fir::ValueR v) -> fir::ValueR {
      auto it = plan.node_of.find(v);
      if (it != plan.node_of.end() &&
          is_vector_shape(plan.nodes[it->second].shape)) {
        return vm.at(v);
      }
      const fir::ValueR scalar = it == plan.node_of.end() ? v : vm.at(v);
      if (auto bc = broadcasts.find(scalar); bc != broadcasts.end()) {
        return bc->second;
      }
      auto res = b.build_vbroadcast(scalar, vec_ty(scalar.get_type()));
      broadcasts[scalar] = res;
      return res;
    };
    const auto cond_instr =
        term->args[0].is_instr() && term->args[0].as_instr()->uses.size() == 1
            ? term->args[0].as_instr()
            : fir::Instr{fir::Instr::invalid()};
    for (u32 k = n_args; k < plan.nodes.size(); k++) {
      const auto &n = plan.nodes[k];
      auto instr = n.value.as_instr();
      switch (n.kind) {
      case LoopNodeKind::Scalar: {
        if (instr == cond_instr) {
          break;
        }
        auto c = b.insert_copy(instr);
        c.substitute(vm);
        vm[n.value] = fir::ValueR{c};
        break;
      }
      case LoopNodeKind::Load:
        vm[n.value] = b.build_load(vec_ty(n.type), vm.at(instr->args[0]), false,
                                   false);
        break;
      case LoopNodeKind::Store:
        b.build_store(vm.at(instr->args[0]), operand(instr->args[1]), false,
                      false);
        break;
      case LoopNodeKind::Binary:
        vm[n.value] = b.build_binary_op(operand(instr->args[0]),
                                        operand(instr->args[1]),
                                        static_cast<BinSub>(n.sub));
        break;
      case LoopNodeKind::Intrinsic:
        vm[n.value] = b.build_intrinsic(
            operand(instr->args[0]),
            static_cast<fir::IntrinsicSubType>(n.sub));
        break;
      default:
        UNREACH();
      }
    }
    for (u32 k = 0; k < n_args; k++) {
      const auto &n = plan.nodes[k];
      if (n.kind == LoopNodeKind::Induction) {
        next_vals.push_back(b.build_int_add(
            vec_args[k], cint(static_cast<i128>(vf) * n.stride,
                              n.type->get_bitwidth())));
      } else {
        next_vals.push_back(vm.at(back_args[k]));
      }
    }
    auto cond = b.build_int_cmp(next_vals[plan.counter], end_counter,
                                fir::ICmpInstrSubType::NE);
    auto br = b.build_cond_branch(cond, vloop, vmid);
    for (u32 k = 0; k < n_args; k++) {
      br.add_bb_arg(0, next_vals[k]);
      br.add_bb_arg(1, next_vals[k]);
    }
  }

  // middle block reduces, then leaves or runs the remainder
  {
    fir::Builder b{vmid};
    b.at_end(vmid);
    TVec<fir::ValueR> finals;
    for (u32 k = 0; k < n_args; k++) {
      const auto &n = plan.nodes[k];
      if (n.kind != LoopNodeKind::Reduction) {
        finals.push_back(mid_args[k]);
        continue;
      }
      const auto sub = static_cast<BinSub>(n.sub);
      const bool prod = sub == BinSub::IntMul || sub == BinSub::FloatMul;
      auto red = b.build_vector_op(mid_args[k], n.type,
                                   prod ? fir::VectorISubType::HorizontalMul
                                        : fir::VectorISubType::HorizontalAdd);
      finals.push_back(b.build_binary_op(red, pre_args[k], sub));
    }
    auto done = b.build_int_cmp(vtrip, total_trip, fir::ICmpInstrSubType::EQ);
    auto br = b.build_cond_branch(done, exit_bb, hdr);
    for (const auto &e : term->bbs[plan.exit_edge].args) {
      fir::ValueR mapped = e;
      for (u32 k = 0; k < n_args; k++) {
        if (e == back_args[k]) {
          mapped = finals[k];
          break;
        }
      }
      br.add_bb_arg(0, mapped);
    }
    for (u32 k = 0; k < n_args; k++) {
      br.add_bb_arg(1, finals[k]);
    }
  }
  return true;
}

bool SLPVectorizer::vectorize_loops(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("LoopVectorize");
  const auto &features = ctx.config->target.features;
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  LoopInfoAnalysis linfo{dom};
  AliasAnalyis aa{};
  bool changed = false;

  // vector loop (vector types) and remainder (two entries) are never candidates
  for (u32 round = 0; round < 64; round++) {
    bool vectorized = false;
    for (auto &loop : linfo.info) {
      ScalarEvo evo{cfg, loop};
      LoopPlan plan;
      if (!build_loop_plan(ctx, cfg, dom, loop, evo, plan)) {
        continue;
      }
      u32 max_vf = max_vector_bits(features) / plan.elem_bits;
      if (config.max_vf != 0) {
        max_vf = std::min(max_vf, config.max_vf);
      }
      u32 best_vf = 0;
      i64 best_cost = 0;
      for (u32 vf = max_vf; vf >= 2; vf /= 2) {
        if (!loop_legal(plan, vf, features)) {
          continue;
        }
        LoopMemDep dep{evo, aa};
        bool deps_ok = true;
        for (size_t i = 0; i < plan.accesses.size() && deps_ok; i++) {
          for (size_t j = i + 1; j < plan.accesses.size() && deps_ok; j++) {
            deps_ok = dep.query(plan.accesses[i], plan.accesses[j], vf) ==
                      MemDep::Independent;
          }
        }
        if (!deps_ok) {
          continue;
        }
        i64 scalar_total = 0;
        const i64 cost = loop_cost(plan, vf, features, config.assumed_trip,
                                     scalar_total);
        if (cost >= scalar_total) {
          continue;
        }
        if (best_vf == 0 || cost < best_cost) {
          best_vf = vf;
          best_cost = cost;
        }
      }
      if (best_vf == 0) {
        continue;
      }
      emit_loop(ctx, func, cfg, evo, plan, best_vf);
      utils::StatCollector::get().addi(1, "loopVectorized",
                                       utils::StatCollector::StatFOptim);
      vectorized = true;
      break;
    }
    if (!vectorized) {
      break;
    }
    changed = true;
    cfg.update(func, false);
    dom.update(cfg);
    linfo.update(dom);
  }
  return changed;
}

} // namespace foptim::optim
