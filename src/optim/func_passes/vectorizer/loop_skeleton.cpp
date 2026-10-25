#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/func_passes/vectorizer/loop.hpp"
#include "optim/func_passes/vectorizer/recipes.hpp"
#include "optim/helper/helper.hpp"
#include "optim/helper/scev_expander.hpp"

namespace foptim::optim::vec {

namespace {
using BinSub = fir::BinaryInstrSubType;

// the value that leaves the accumulator unchanged
fir::ValueR reduction_identity(fir::Context &ctx, const PlanNode &n) {
  const auto ty = n.type;
  const u32 bw = ty->get_bitwidth();
  if (ty->is_int()) {
    const i128 smax = (static_cast<i128>(1) << (bw - 1)) - 1;
    i128 v = 0;
    switch (n.red) {
      case RedKind::Mul:
        v = 1;
        break;
      case RedKind::And:
      case RedKind::UMin:
        v = -1;
        break;
      case RedKind::SMax:
        v = -smax - 1;
        break;
      case RedKind::SMin:
        v = smax;
        break;
      default:
        break;
    }
    return fir::ValueR{ctx->get_constant_int(v, bw)};
  }
  const bool mul = n.red == RedKind::Mul;
  if (bw == 32) {
    return fir::ValueR{ctx->get_constant_value(mul ? 1.0F : 0.0F, ty)};
  }
  return fir::ValueR{ctx->get_constant_value(mul ? 1.0 : 0.0, ty)};
}

// the horizontal operation that folds the lanes of an accumulator
fir::ReduceInstrSubType reduction_horizontal(const PlanNode &n) {
  using R = fir::ReduceInstrSubType;
  switch (n.red) {
    case RedKind::Add:
      return R::Add;
    case RedKind::Mul:
      return R::Mul;
    case RedKind::And:
      return R::And;
    case RedKind::Or:
      return R::Or;
    case RedKind::Xor:
      return R::Xor;
    case RedKind::SMin:
      return R::SMin;
    case RedKind::SMax:
      return R::SMax;
    case RedKind::UMin:
      return R::UMin;
    case RedKind::UMax:
      return R::UMax;
  }
  UNREACH();
}

// the accumulator operation applied to two scalars
fir::ValueR reduction_combine(fir::Builder &b, const PlanNode &n, fir::ValueR x,
                              fir::ValueR y) {
  const bool fp = n.type->is_float();
  switch (n.red) {
    case RedKind::Add:
      return b.build_binary_op(x, y, fp ? BinSub::FloatAdd : BinSub::IntAdd);
    case RedKind::Mul:
      return b.build_binary_op(x, y, fp ? BinSub::FloatMul : BinSub::IntMul);
    case RedKind::And:
      return b.build_binary_op(x, y, BinSub::And);
    case RedKind::Or:
      return b.build_binary_op(x, y, BinSub::Or);
    case RedKind::Xor:
      return b.build_binary_op(x, y, BinSub::Xor);
    case RedKind::SMin:
      return b.build_intrinsic(x, y, fir::IntrinsicSubType::SMin);
    case RedKind::SMax:
      return b.build_intrinsic(x, y, fir::IntrinsicSubType::SMax);
    case RedKind::UMin:
      return b.build_intrinsic(x, y, fir::IntrinsicSubType::UMin);
    case RedKind::UMax:
      return b.build_intrinsic(x, y, fir::IntrinsicSubType::UMax);
  }
  UNREACH();
}
}  // namespace

LoopBlocks emit_loop(fir::Context &ctx, Plan &plan) {
  ScalarEvo &evo = *plan.evo;
  const auto &l = plan.loop;
  const u32 vf = plan.vf;
  auto hdr = l.header;
  auto term = l.latch->get_terminator();
  auto entry = evo.entry_block();
  const u32 n_args = static_cast<u32>(hdr->args.size());
  const auto &back_args = term->bbs[l.back_edge].args;
  auto exit_bb = term->bbs[l.exit_edge].bb;
  const auto trip_bw = evo.expr(l.trip).bw;

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
    const auto ty = n.kind == NodeKind::Reduction ? vec_ty(n.type) : n.type;
    vec_args.push_back(add_arg(vloop, ty));
    mid_args.push_back(add_arg(vmid, ty));
  }

  // header executions, computed in the entry block
  fir::ValueR total_trip;
  {
    fir::ValueR last_iter;
    {
      SCEVExpander ex{ctx, evo, entry->get_terminator()};
      last_iter = ex.expand(l.trip);
    }
    fir::Builder b{entry->get_terminator()};
    total_trip = b.build_int_add(last_iter, cint(1, trip_bw));
  }
  entry->get_terminator().replace_bb(hdr, vpre, true);

  // pre block, vtrip is the trip count rounded down to a multiple of vf
  fir::ValueR vtrip;
  fir::ValueR end_counter;
  {
    fir::Builder b{vpre};
    b.at_end(vpre);
    vtrip = b.build_binary_op(
        total_trip, cint(~static_cast<i128>(vf - 1), trip_bw), BinSub::And);
    end_counter = b.build_int_add(pre_args[l.counter], vtrip);
    auto none =
        b.build_int_cmp(vtrip, cint(0, trip_bw), fir::ICmpInstrSubType::EQ);
    TVec<fir::ValueR> vec_init;
    for (u32 k = 0; k < n_args; k++) {
      const auto &n = plan.nodes[k];
      if (n.kind == NodeKind::Reduction) {
        auto id = reduction_identity(ctx, n);
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
      auto it = l.node_of.find(v);
      if (it != l.node_of.end() &&
          is_vector_shape(plan.nodes[it->second].shape)) {
        return vm.at(v);
      }
      const fir::ValueR scalar = it == l.node_of.end() ? v : vm.at(v);
      if (auto bc = broadcasts.find(scalar); bc != broadcasts.end()) {
        return bc->second;
      }
      const bool in_loop =
          is_cheap_splat_constant(scalar) ||
          (scalar.is_instr() && scalar.as_instr()->get_parent() == vloop) ||
          (scalar.is_bb_arg() && scalar.as_bb_arg()->get_parent() == vloop);
      fir::ValueR res;
      if (in_loop) {
        res = b.build_vbroadcast(scalar, vec_ty(scalar.get_type()));
      } else {
        fir::Builder pb{vpre};
        pb.at_penultimate(vpre);
        res = pb.build_vbroadcast(scalar, vec_ty(scalar.get_type()));
      }
      broadcasts[scalar] = res;
      return res;
    };
    // the mask of a block and of an edge, built on first use
    TVec<std::optional<fir::ValueR>> block_masks(l.blocks.size());
    TVec<bool> block_mask_done(l.blocks.size(), false);
    auto mask_not = [&](fir::ValueR m) {
      return b.build_unary_op(m, fir::UnaryInstrSubType::Not);
    };
    auto block_mask = [&](auto &&self, u32 bi) -> std::optional<fir::ValueR> {
      if (block_mask_done[bi]) {
        return block_masks[bi];
      }
      const auto &blk = l.blocks[bi];
      std::optional<fir::ValueR> res;
      if (blk.always) {
        res = std::nullopt;
      } else if (blk.same_mask_as != ~0U) {
        res = self(self, blk.same_mask_as);
      } else {
        for (const auto &e : blk.in_edges) {
          auto pt = l.blocks[e.from].bb->get_terminator();
          auto pm = self(self, e.from);
          std::optional<fir::ValueR> em = pm;
          if (pt->is(fir::InstrType::CondBranchInstr)) {
            fir::ValueR c = vm.at(pt->args[0]);
            if (e.slot == 1) {
              c = mask_not(c);
            }
            em = pm ? b.build_binary_op(*pm, c, BinSub::And) : c;
          }
          res = res ? b.build_binary_op(*res, *em, BinSub::Or) : *em;
        }
      }
      block_masks[bi] = res;
      block_mask_done[bi] = true;
      return res;
    };
    // mask of the lanes that take the edge
    auto edge_mask = [&](const LoopEdge &e) {
      auto pt = l.blocks[e.from].bb->get_terminator();
      auto pm = block_mask(block_mask, e.from);
      std::optional<fir::ValueR> em = pm;
      if (pt->is(fir::InstrType::CondBranchInstr)) {
        fir::ValueR c = vm.at(pt->args[0]);
        if (e.slot == 1) {
          c = mask_not(c);
        }
        em = pm ? b.build_binary_op(*pm, c, BinSub::And) : c;
      }
      return *em;
    };
    auto zero_vec = [&](fir::TypeR elem) {
      const auto vty = vec_ty(elem);
      IRVec<fir::ConstantValueR> members;
      for (u32 i = 0; i < vf; i++) {
        members.push_back(elem->is_int()
                              ? ctx->get_constant_int(0, elem->get_bitwidth())
                              : (elem->get_bitwidth() == 32
                                     ? ctx->get_constant_value(0.0F, elem)
                                     : ctx->get_constant_value(0.0, elem)));
      }
      return fir::ValueR{ctx->get_constant_value(std::move(members), vty)};
    };
    const auto cond_instr =
        term->args[0].is_instr() && term->args[0].as_instr()->uses.size() == 1
            ? term->args[0].as_instr()
            : fir::Instr{fir::Instr::invalid()};
    for (u32 k = n_args; k < plan.nodes.size(); k++) {
      const auto &n = plan.nodes[k];
      if (n.kind == NodeKind::Phi && !is_vector_shape(n.shape)) {
        // every edge passes the same scalar
        const auto &in = l.blocks[n.block].in_edges;
        auto et = l.blocks[in[0].from].bb->get_terminator();
        const auto v = et->bbs[in[0].slot].args[n.arg_id];
        vm[n.value] = l.node_of.contains(v) ? vm.at(v) : v;
        continue;
      }
      if (n.kind == NodeKind::Phi) {
        // the value of the edge that was taken, the last edge is the default
        const auto &in = l.blocks[n.block].in_edges;
        auto edge_value = [&](const LoopEdge &e) {
          auto et = l.blocks[e.from].bb->get_terminator();
          return operand(et->bbs[e.slot].args[n.arg_id]);
        };
        fir::ValueR acc = edge_value(in.back());
        for (size_t i = in.size() - 1; i > 0; i--) {
          acc = b.build_select(vec_ty(n.type), edge_mask(in[i - 1]),
                               edge_value(in[i - 1]), acc);
        }
        vm[n.value] = acc;
        continue;
      }
      auto instr = n.value.as_instr();
      const auto mask = l.blocks[n.block].always
                            ? std::nullopt
                            : block_mask(block_mask, n.block);
      switch (n.kind) {
        case NodeKind::Scalar: {
          if (instr == cond_instr) {
            break;
          }
          auto c = b.insert_copy(instr, fir::MetaCopy::Types);
          c.substitute(vm);
          vm[n.value] = c.result();
          break;
        }
        case NodeKind::Load: {
          const fir::ValueR ops[] = {vm.at(instr->args[0])};
          if (mask) {
            vm[n.value] = b.build_intrinsic(ops[0], *mask, zero_vec(n.type),
                                            fir::IntrinsicSubType::MaskedLoad,
                                            vec_ty(n.type));
            break;
          }
          vm[n.value] = emit_node(ctx, b, n, vf, ops);
          break;
        }
        case NodeKind::Store: {
          const fir::ValueR ops[] = {vm.at(instr->args[0]),
                                     operand(instr->args[1])};
          if (mask) {
            b.build_intrinsic(ops[0], *mask, ops[1],
                              fir::IntrinsicSubType::MaskedStore,
                              ctx->get_void_type());
            break;
          }
          emit_node(ctx, b, n, vf, ops);
          break;
        }
        case NodeKind::Binary: {
          const fir::ValueR ops[] = {operand(instr->args[0]),
                                     operand(instr->args[1])};
          vm[n.value] = emit_node(ctx, b, n, vf, ops);
          break;
        }
        case NodeKind::Cmp:
        case NodeKind::Select: {
          TVec<fir::ValueR> ops;
          for (auto a : instr->args) {
            ops.push_back(operand(a));
          }
          vm[n.value] = emit_node(ctx, b, n, vf, ops);
          break;
        }
        case NodeKind::Zext:
        case NodeKind::Unary:
        case NodeKind::Intrin: {
          TVec<fir::ValueR> ops;
          for (auto a : instr->args) {
            ops.push_back(operand(a));
          }
          vm[n.value] = emit_node(ctx, b, n, vf, ops);
          break;
        }
        default:
          UNREACH();
      }
    }
    for (u32 k = 0; k < n_args; k++) {
      const auto &n = plan.nodes[k];
      if (n.kind == NodeKind::Induction && n.type->is_ptr()) {
        next_vals.push_back(b.build_ptr_add(
            vec_args[k], cint(static_cast<i128>(vf) * n.stride, 64)));
      } else if (n.kind == NodeKind::Induction) {
        next_vals.push_back(b.build_int_add(
            vec_args[k],
            cint(static_cast<i128>(vf) * n.stride, n.type->get_bitwidth())));
      } else {
        next_vals.push_back(vm.at(back_args[k]));
      }
    }
    auto cond = b.build_int_cmp(next_vals[l.counter], end_counter,
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
      if (n.kind != NodeKind::Reduction) {
        finals.push_back(mid_args[k]);
        continue;
      }
      auto red = b.build_reduce(mid_args[k], reduction_horizontal(n));
      finals.push_back(reduction_combine(b, n, red, pre_args[k]));
    }
    auto done = b.build_int_cmp(vtrip, total_trip, fir::ICmpInstrSubType::EQ);
    auto br = b.build_cond_branch(done, exit_bb, hdr);
    for (const auto &e : term->bbs[l.exit_edge].args) {
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
  return {vpre, vloop, vmid};
}

}  // namespace foptim::optim::vec