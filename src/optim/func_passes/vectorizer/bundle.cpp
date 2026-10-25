#include "optim/func_passes/vectorizer/bundle.hpp"

#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/func_passes/vectorizer/recipes.hpp"
#include "optim/helper/WFVector.hpp"

namespace foptim::optim::vec {

namespace {
bool is_mask(fir::TypeR ty) { return ty->is_int() && ty->as_int() == 1; }
constexpr u32 NoNode = std::numeric_limits<u32>::max();
constexpr bool debug_print = false;
// bound on the compile time of one tree, bigger trees are not tried
constexpr size_t MaxBundleNodes = 512;

// Reduction for what the backend does not lower: extract the lanes and combine
// them as scalars (binary operations first combine the halves of a 256 bit
// vector).
fir::ValueR fold_lanes(fir::Context &ctx, fir::Builder &bb, fir::ValueR vec,
                       fir::Instr orig) {
  const auto elem = orig->result_type();
  const bool binary = orig->is(fir::InstrType::BinaryInstr);
  auto combine = [&](fir::ValueR a, fir::ValueR b) {
    if (binary) {
      return bb.build_binary_op(
          a, b, static_cast<fir::BinaryInstrSubType>(orig->subtype));
    }
    return bb.build_intrinsic(
        a, b, static_cast<fir::IntrinsicSubType>(orig->subtype));
  };
  auto vty = vec.get_type()->as_vec();
  u32 lanes = static_cast<u32>(vty.member_number);
  if (binary && lanes * elem->get_bitwidth() == 256) {
    lanes /= 2;
    auto half = ctx->get_vec_type(elem, lanes);
    auto lo = bb.build_vector_op(vec, half, fir::VectorISubType::ExtractLow);
    auto hi = bb.build_vector_op(vec, half, fir::VectorISubType::ExtractHigh);
    vec = combine(lo, hi);
  }
  fir::ValueR acc;
  for (u32 i = 0; i < lanes; i++) {
    fir::ValueR idx[1] = {fir::ValueR{ctx->get_constant_int(i, 32)}};
    auto x = bb.build_extract_element(vec, idx, elem);
    acc = i == 0 ? x : combine(acc, x);
  }
  return acc;
}

struct Neutral {
  f64 f = 0.F;
  i128 i = 0;
};

// The value that lets a lane without the operation pass its other operand
// through. TODO: for sub and the shifts this only holds for the right operand.
Neutral neutral_element(fir::BinaryInstrSubType op) {
  switch (op) {
    case fir::BinaryInstrSubType::FloatAdd:
    case fir::BinaryInstrSubType::IntAdd:
    case fir::BinaryInstrSubType::PtrAdd:
    case fir::BinaryInstrSubType::Shl:
    case fir::BinaryInstrSubType::Shr:
    case fir::BinaryInstrSubType::AShr:
    case fir::BinaryInstrSubType::IntSub:
    case fir::BinaryInstrSubType::Xor:
    case fir::BinaryInstrSubType::Or:
      return {.f = 0.F, .i = 0};
    case fir::BinaryInstrSubType::FloatMul:
    case fir::BinaryInstrSubType::IntMul:
      return {.f = 1.F, .i = 1};
    case fir::BinaryInstrSubType::And:
      return {.f = std::bit_cast<f64>(~static_cast<u64>(0)),
              .i = ~static_cast<i128>(0)};
    case fir::BinaryInstrSubType::INVALID:
    case fir::BinaryInstrSubType::IntSRem:
    case fir::BinaryInstrSubType::IntURem:
    case fir::BinaryInstrSubType::IntSDiv:
    case fir::BinaryInstrSubType::IntUDiv:
    case fir::BinaryInstrSubType::FloatSub:
    case fir::BinaryInstrSubType::FloatDiv:
      UNREACH();
  }
  UNREACH();
}

fir::ValueR emit_tree(fir::Context &ctx, const Plan &plan, u32 id,
                      const Seed &seed) {
  const auto &n = plan.nodes[id];
  auto emit_child = [&](size_t i) {
    return emit_tree(ctx, plan, n.children.at(i), seed);
  };
  switch (n.kind) {
    case NodeKind::Broadcast: {
      fir::Builder bb{n.insert_loc};
      if (n.after) {
        bb.after(n.insert_loc);
      }
      return bb.build_vbroadcast(
          n.value, ctx->get_vec_type(n.value.get_type(), n.n_lanes));
    }
    case NodeKind::Gather: {
      fir::Builder bb{n.insert_loc};
      if (n.after) {
        bb.after(n.insert_loc);
      }
      const auto vty = ctx->get_vec_type(n.type, n.n_lanes);
      fir::ValueR vec = zero_vector(ctx, n.type, n.n_lanes);
      for (u32 i = 0; i < n.n_lanes; i++) {
        fir::ValueR idx[1] = {fir::ValueR{ctx->get_constant_int(i, 32)}};
        vec = bb.build_insert_element(vec, n.lanes[i], idx, vty);
      }
      return vec;
    }
    case NodeKind::BuildRoot: {
      auto vec = emit_child(0);
      seed.base.as_instr()->replace_all_uses(vec);
      return vec;
    }
    case NodeKind::HorizRed: {
      auto orig_instr = seed.base.as_instr();
      ASSERT(n.children.size() == 1);
      const auto &features = ctx.config->target.features;
      const auto red_op = reduce_op_for(orig_instr, n.n_lanes, features);
      auto vec = emit_child(0);
      fir::Builder bb{orig_instr};
      bb.after(orig_instr);
      auto new_v = red_op ? bb.build_reduce(vec, *red_op)
                          : fold_lanes(ctx, bb, vec, orig_instr);
      orig_instr->replace_all_uses(new_v);
      return new_v;
    }
    case NodeKind::Call: {
      // children are the arguments in reverse order
      TVec<fir::ValueR> args;
      args.resize(n.children.size());
      for (size_t i = 0; i < n.children.size(); i++) {
        args[n.children.size() - 1 - i] = emit_child(i);
      }
      fir::Builder bb{n.insert_loc};
      auto *funcy = n.value.as_constant()->as_func().func;
      auto new_func = whole_function_vectorize(*funcy, n.n_lanes);
      ASSERT(new_func.has_value())
      auto ftype = new_func.value()->func_ty;
      return bb.build_call(
          fir::ValueR{ctx->get_constant_value(new_func.value())}, ftype,
          ftype->as_func().return_type(), args);
    }
    case NodeKind::Extract:
      return n.value.as_instr()->args[0];
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
    case NodeKind::Load:
      if (!n.gather_idx.empty()) {
        fir::Builder bb{n.insert_loc};
        bb.after(n.insert_loc);
        // every lane reads the same address
        if (std::ranges::all_of(n.gather_idx,
                                [&](i64 i) { return i == n.gather_idx[0]; })) {
          auto scalar = bb.build_load(n.type, n.aux->args[0], false, false);
          return bb.build_vbroadcast(scalar,
                                     ctx->get_vec_type(n.type, n.n_lanes));
        }
        if (!n.shuf_mask.empty()) {
          const auto vty = ctx->get_vec_type(n.type, n.n_lanes);
          const i64 elem_bytes = n.type->get_size();
          fir::ValueR loads[2];
          for (size_t i = 0; i < n.shuf_starts.size(); i++) {
            auto ptr = bb.build_ptr_add(
                n.value, fir::ValueR{ctx->get_constant_int(
                             n.shuf_starts[i] * elem_bytes, 64)});
            loads[i] = bb.build_load(vty, ptr, false, false);
          }
          IRVec<fir::ConstantValueR> mask;
          for (auto m : n.shuf_mask) {
            mask.push_back(ctx->get_constant_int(m, 32));
          }
          auto mask_v = fir::ValueR{ctx->get_constant_value(
              std::move(mask),
              ctx->get_vec_type(ctx->get_int_type(32), n.n_lanes))};
          const auto second = n.shuf_starts.size() == 2 ? loads[1] : loads[0];
          return bb.build_vector_op(loads[0], second, mask_v, vty,
                                    fir::VectorISubType::Shuffle);
        }
        const u32 idx_bits =
            n.type->get_bitwidth() == 64 && n.n_lanes == 2 ? 64 : 32;
        IRVec<fir::ConstantValueR> idx;
        for (auto i : n.gather_idx) {
          idx.push_back(ctx->get_constant_int(i, idx_bits));
        }
        const auto vty = ctx->get_vec_type(n.type, n.n_lanes);
        auto idx_v = fir::ValueR{ctx->get_constant_value(
            std::move(idx),
            ctx->get_vec_type(ctx->get_int_type(idx_bits), n.n_lanes))};
        return bb.build_intrinsic(n.value, idx_v, fir::IntrinsicSubType::Gather,
                                  vty);
      }
      [[fallthrough]];
    case NodeKind::Store: {
      auto val = emit_child(0);
      fir::Builder bb{n.insert_loc};
      bb.after(n.insert_loc);
      if (n.kind == NodeKind::Store) {
        const fir::ValueR ops[] = {n.aux->args[0], val};
        return emit_node(ctx, bb, n, n.n_lanes, ops);
      }
      const fir::ValueR ops[] = {val};
      return emit_node(ctx, bb, n, n.n_lanes, ops);
    }
    default: {
      // the children are in reverse operand order
      TVec<fir::ValueR> ops;
      ops.resize(n.children.size());
      for (size_t i = 0; i < n.children.size(); i++) {
        ops[n.children.size() - 1 - i] = emit_child(i);
      }
      fir::Builder bb{n.insert_loc};
      return emit_node(ctx, bb, n, n.n_lanes, ops);
    }
  }
}
}  // namespace

bool plan_bundle(fir::Context &ctx, const Features &features, Seed &b,
                 bool reduction, Plan &plan) {
  AliasAnalyis aa{};
  auto &info = *b.block;
  plan.space = LaneSpace::Bundle;
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
    init_back(red, base, InitCtx{.info = info, .parent_loc = {}});
    red.n_lanes = static_cast<u32>(b.data.size());
    red.value = b.base;
    root_parent = add_node(NoNode, std::move(red));
  }
  TVec<std::pair<u32, TVec<fir::ValueR>>> worklist;
  if (b.kind == SeedKind::Build) {
    PlanNode root;
    root.kind = NodeKind::BuildRoot;
    init_build_root(root, b.lane_values, b.type,
                    InitCtx{.info = info, .parent_loc = b.data[0].instr});
    root.scalar_ops = b.data[0].build_instrs;
    root_parent = add_node(NoNode, std::move(root));
    worklist.emplace_back(root_parent, b.lane_values);
  } else {
    TVec<fir::ValueR> v;
    for (auto &va : b.data) {
      v.push_back(va.instr.result());
    }
    worklist.emplace_back(root_parent, std::move(v));
  }

  while (!worklist.empty()) {
    if (plan.nodes.size() > MaxBundleNodes) {
      return false;
    }
    auto [parent, curr] = std::move(worklist.back());
    worklist.pop_back();
    auto test_i = curr.back();

    if (broadcast_match(curr)) {
      PlanNode n;
      n.kind = NodeKind::Broadcast;
      broadcast_init(n, curr);
      ASSERT(parent != NoNode);
      add_node(parent, std::move(n));
      continue;
    }
    if (!lane_instr(test_i) && lane_instr(curr.front())) {
      test_i = curr.front();
    }

    if (lane_instr(test_i)) {
      const Recipe *recipe = find_recipe(test_i.as_instr()->instr_type);
      BundleCtx rctx{.info = info, .aa = aa, .features = features};
      if (recipe == nullptr || !recipe->match_bundle(curr, rctx)) {
        if (parent != NoNode && gather_legal(curr, features)) {
          PlanNode n;
          n.kind = NodeKind::Gather;
          gather_init(n, curr,
                      InitCtx{.info = info,
                              .parent_loc = plan.nodes[parent].insert_loc});
          add_node(parent, std::move(n));
          continue;
        }
        if constexpr (debug_print) {
          fmt::println(
              "Failed tree vectorize at {} like {}",
              recipe != nullptr ? recipe->name : "unsupported instruction",
              curr.back());
        }
        return false;
      }
      PlanNode n;
      n.kind = recipe->kind;
      InitCtx ictx{.info = info,
                   .parent_loc = parent != NoNode
                                     ? plan.nodes[parent].insert_loc
                                     : fir::Instr{}};
      recipe->init(n, curr, ictx);
      const bool built_store = recipe->kind == NodeKind::Store &&
                               std::ranges::any_of(b.data, [](const auto &d) {
                                 return d.lanes > 1;
                               });
      if (built_store) {
        // a store of a built vector covers all its lanes
        n.n_lanes = b.n_lanes();
        n.type = b.type;
        for (const auto &d : b.data) {
          n.scalar_ops += 2 + d.build_instrs;
        }
      }
      // a widened value needs a vector type the backend has, a store checks
      // the type of its value operand, masks the width of the compares
      const bool vector_result = n.kind != NodeKind::Store &&
                                 n.kind != NodeKind::HorizRed &&
                                 n.kind != NodeKind::Extract &&
                                 n.kind != NodeKind::Cmp && !is_mask(n.type);
      if (vector_result && !vector_type_ok(n.type, n.n_lanes, features)) {
        if constexpr (debug_print) {
          fmt::println("Failed tree vectorize, no {} lane vector for {}",
                       n.n_lanes, curr.back());
        }
        return false;
      }
      if (!recipe->legal(n, LaneSpace::Bundle, n.n_lanes, features)) {
        if constexpr (debug_print) {
          fmt::println("Failed tree vectorize, {} is not legal like {}",
                       recipe->name, curr.back());
        }
        return false;
      }
      auto n_args = recipe->n_args(curr);
      if (recipe->kind == NodeKind::Load && !n.gather_idx.empty()) {
        n_args = 0;
      }
      if (recipe->kind == NodeKind::Store) {
        ASSERT(parent == NoNode);
      } else {
        ASSERT(parent != NoNode);
      }
      auto bin_op = static_cast<fir::BinaryInstrSubType>(n.sub);
      auto result = add_node(parent, std::move(n));

      if (recipe->kind == NodeKind::Binary) {
        Neutral neutral;
        if (rctx.potential_neutral) {
          neutral = neutral_element(bin_op);
        }
        auto arg_0 = test_i.as_instr()->args[0];
        for (size_t arg_id = 0; arg_id < n_args; arg_id++) {
          TVec<fir::ValueR> data;
          for (auto c : curr) {
            if (c == arg_0 && arg_id == 1 && test_i != c) {
              auto t = test_i.as_instr()->args[1].get_type();
              if (t->is_float()) {
                data.emplace_back(ctx->get_constant_value(neutral.f, t));
              } else {
                data.emplace_back(ctx->get_constant_value(neutral.i, t));
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
      if (built_store) {
        // the value operand is the list of scalars of the builds
        TVec<fir::ValueR> data;
        for (const auto &d : b.data) {
          if (d.lanes > 1) {
            BuildInfo unused;
            const bool ok = build_lanes(d.instr->args[1], data, unused);
            ASSERT(ok);
          } else {
            data.push_back(d.instr->args[1]);
          }
        }
        worklist.emplace_back(result, std::move(data));
        continue;
      }
      // the callee is not an operand
      for (size_t arg_id = recipe->kind == NodeKind::Call ? 1 : 0;
           arg_id < n_args; arg_id++) {
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
    if (parent != NoNode && gather_legal(curr, features)) {
      PlanNode n;
      n.kind = NodeKind::Gather;
      gather_init(
          n, curr,
          InitCtx{.info = info, .parent_loc = plan.nodes[parent].insert_loc});
      add_node(parent, std::move(n));
      continue;
    }
    if constexpr (debug_print) {
      fmt::println("Failed tree vectorize at something like-> {:cd}",
                   curr.back());
    }
    return false;
  }
  plan.vf = reduction                   ? static_cast<u32>(b.data.size())
            : b.kind == SeedKind::Build ? static_cast<u32>(b.lane_values.size())
                                        : b.n_lanes();
  plan.benefit = plan.nodes.empty() ? 0 : bundle_benefit(plan, 0, features);
  if constexpr (debug_print) {
    node_dump(plan, 0);
  }
  return true;
}

namespace {
// destroys the links of a build once nothing uses them
void destroy_dead_build(fir::ValueR v) {
  if (!lane_instr(v) || v.get_n_uses() != 0) {
    return;
  }
  auto instr = v.as_instr();
  const bool link = instr->is(fir::InstrType::InsertElement) ||
                    instr->is(fir::VectorISubType::Concat) ||
                    instr->is(fir::VectorISubType::Shuffle) ||
                    instr->is(fir::VectorISubType::Broadcast);
  if (!link) {
    return;
  }
  TVec<fir::ValueR> operands;
  for (auto a : instr->args) {
    operands.push_back(a);
  }
  instr.destroy();
  for (auto a : operands) {
    destroy_dead_build(a);
  }
}
}  // namespace

void apply_bundle(fir::Context &ctx, const Plan &plan, Seed &seed,
                  bool reduction) {
  // emit first, erase afterwards: later nodes may still read the scalars
  emit_tree(ctx, plan, 0, seed);
  if (reduction) {
    seed.base.as_instr().destroy();
  } else {
    TVec<fir::ValueR> built;
    for (auto &d : seed.data) {
      if (d.lanes > 1 && seed.kind == SeedKind::Store) {
        built.push_back(d.instr->args[1]);
      }
      if (seed.kind == SeedKind::Build) {
        for (auto a : d.instr->args) {
          built.push_back(a);
        }
      }
      d.instr.destroy();
    }
    for (auto v : built) {
      destroy_dead_build(v);
    }
  }
}

}  // namespace foptim::optim::vec
