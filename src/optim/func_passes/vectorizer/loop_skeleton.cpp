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
  using Parts = TVec<fir::ValueR>;
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

  TVec<u32> pl;
  const bool parts_ok =
      assign_part_lanes(plan, vf, ctx.config->target.features, pl);
  ASSERT(parts_ok);

  auto add_arg = [&](fir::BasicBlock bb, fir::TypeR ty) {
    return fir::ValueR{bb.add_arg(ctx->storage.insert_bb_arg(bb, ty))};
  };
  auto vec_ty = [&](fir::TypeR elem, u32 lanes) {
    return ctx->get_vec_type(elem, lanes);
  };
  auto cint = [&](i128 v, u32 bw) {
    return fir::ValueR{ctx->get_constant_int(v, bw)};
  };

  fir::Builder blocks{hdr};
  auto vpre = blocks.append_bb();
  auto vloop = blocks.append_bb();
  auto vmid = blocks.append_bb();

  // the vector loop only reaches addresses the scalar loop reaches, so its
  // pointer steps keep the wrap flags of the scalar step
  auto ptr_add_like_step = [&](fir::Builder &bld, u32 k, fir::ValueR ptr,
                               fir::ValueR off) {
    bool inb = false;
    bool nuw = false;
    bool nsw = false;
    const auto step = back_args[k];
    if (step.is_instr() && step.as_instr()->is(BinSub::PtrAdd)) {
      inb = step.as_instr()->InBounds;
      nuw = step.as_instr()->NUW;
      nsw = step.as_instr()->NSW;
    }
    return bld.build_ptr_add(ptr, off, inb, nuw, nsw);
  };

  TVec<fir::ValueR> pre_args;
  // an accumulator has one argument per part
  TVec<Parts> vec_args(n_args);
  TVec<Parts> mid_args(n_args);
  for (u32 k = 0; k < n_args; k++) {
    const auto &n = plan.nodes[k];
    pre_args.push_back(add_arg(vpre, n.type));
    if (n.kind == NodeKind::Reduction) {
      for (u32 p = 0; p < vf / pl[k]; p++) {
        vec_args[k].push_back(add_arg(vloop, vec_ty(n.type, pl[k])));
        mid_args[k].push_back(add_arg(vmid, vec_ty(n.type, pl[k])));
      }
    } else {
      vec_args[k].push_back(add_arg(vloop, n.type));
      mid_args[k].push_back(add_arg(vmid, n.type));
    }
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
    if (plan.nodes[l.counter].type->is_ptr()) {
      end_counter = ptr_add_like_step(
          b, l.counter, pre_args[l.counter],
          b.build_int_mul(vtrip, cint(plan.nodes[l.counter].stride, 64)));
    } else {
      end_counter = b.build_int_add(pre_args[l.counter], vtrip);
    }
    auto none =
        b.build_int_cmp(vtrip, cint(0, trip_bw), fir::ICmpInstrSubType::EQ);
    TVec<Parts> vec_init(n_args);
    for (u32 k = 0; k < n_args; k++) {
      const auto &n = plan.nodes[k];
      if (n.kind == NodeKind::Reduction) {
        auto id = reduction_identity(ctx, n);
        auto bc = b.build_vbroadcast(id, vec_ty(n.type, pl[k]));
        vec_init[k].assign(vf / pl[k], bc);
      } else {
        vec_init[k].push_back(pre_args[k]);
      }
    }
    auto br = b.build_cond_branch(none, hdr, vloop);
    for (u32 k = 0; k < n_args; k++) {
      br.add_bb_arg(0, pre_args[k]);
      for (auto v : vec_init[k]) {
        br.add_bb_arg(1, v);
      }
    }
  }

  TVec<Parts> next_vals(n_args);
  {
    fir::Builder b{vloop};
    b.at_end(vloop);
    // scalars, and the first lane values of the induction variables
    TMap<fir::ValueR, fir::ValueR> vm;
    // vector values, one entry per register
    TMap<fir::ValueR, Parts> vp;
    struct Splat {
      u32 lanes;
      fir::ValueR value;
    };
    TMap<fir::ValueR, Splat> broadcasts;
    for (u32 k = 0; k < n_args; k++) {
      if (plan.nodes[k].kind == NodeKind::Reduction) {
        vp[plan.nodes[k].value] = vec_args[k];
      } else {
        vm[plan.nodes[k].value] = vec_args[k][0];
      }
    }
    // the parts with part_lanes lanes each of a value
    auto operand = [&](fir::ValueR v, u32 part_lanes) -> Parts {
      auto it = l.node_of.find(v);
      if (it != l.node_of.end() &&
          is_vector_shape(plan.nodes[it->second].shape)) {
        ASSERT(pl[it->second] == part_lanes);
        return vp.at(v);
      }
      const fir::ValueR scalar = it == l.node_of.end() ? v : vm.at(v);
      if (auto bc = broadcasts.find(scalar);
          bc != broadcasts.end() && bc->second.lanes == part_lanes) {
        return Parts(vf / part_lanes, bc->second.value);
      }
      const bool in_loop =
          is_cheap_splat_constant(scalar) ||
          (scalar.is_instr() && scalar.as_instr()->get_parent() == vloop) ||
          (scalar.is_bb_arg() && scalar.as_bb_arg()->get_parent() == vloop);
      fir::ValueR res;
      if (in_loop) {
        res = b.build_vbroadcast(scalar, vec_ty(scalar.get_type(), part_lanes));
      } else {
        fir::Builder pb{vpre};
        pb.at_penultimate(vpre);
        res =
            pb.build_vbroadcast(scalar, vec_ty(scalar.get_type(), part_lanes));
      }
      broadcasts[scalar] = {part_lanes, res};
      return Parts(vf / part_lanes, res);
    };
    // the mask of a block and of an edge, built on first use
    TVec<std::optional<Parts>> block_masks(l.blocks.size());
    TVec<bool> block_mask_done(l.blocks.size(), false);
    auto mask_not = [&](const Parts &m) {
      Parts r;
      for (auto v : m) {
        r.push_back(b.build_unary_op(v, fir::UnaryInstrSubType::Not));
      }
      return r;
    };
    auto mask_op = [&](const Parts &x, const Parts &y, BinSub sub) {
      Parts r;
      for (size_t p = 0; p < x.size(); p++) {
        r.push_back(b.build_binary_op(x[p], y[p], sub));
      }
      return r;
    };
    auto edge_mask_of = [&](const LoopEdge &e, std::optional<Parts> pm) {
      auto pt = l.blocks[e.from].bb->get_terminator();
      std::optional<Parts> em = pm;
      if (pt->is(fir::InstrType::CondBranchInstr)) {
        Parts c = vp.at(pt->args[0]);
        if (e.slot == 1) {
          c = mask_not(c);
        }
        em = pm ? mask_op(*pm, c, BinSub::And) : c;
      }
      return *em;
    };
    auto block_mask = [&](auto &&self, u32 bi) -> std::optional<Parts> {
      if (block_mask_done[bi]) {
        return block_masks[bi];
      }
      const auto &blk = l.blocks[bi];
      std::optional<Parts> res;
      if (blk.always) {
        res = std::nullopt;
      } else if (blk.same_mask_as != ~0U) {
        res = self(self, blk.same_mask_as);
      } else {
        for (const auto &e : blk.in_edges) {
          auto pm = self(self, e.from);
          Parts em = edge_mask_of(e, pm);
          res = res ? mask_op(*res, em, BinSub::Or) : em;
        }
      }
      block_masks[bi] = res;
      block_mask_done[bi] = true;
      return res;
    };
    // mask of the lanes that take the edge
    auto edge_mask = [&](const LoopEdge &e) {
      return edge_mask_of(e, block_mask(block_mask, e.from));
    };
    auto zero_vec = [&](fir::TypeR elem, u32 lanes) {
      const auto vty = vec_ty(elem, lanes);
      IRVec<fir::ConstantValueR> members;
      for (u32 i = 0; i < lanes; i++) {
        members.push_back(elem->is_int()
                              ? ctx->get_constant_int(0, elem->get_bitwidth())
                              : (elem->get_bitwidth() == 32
                                     ? ctx->get_constant_value(0.0F, elem)
                                     : ctx->get_constant_value(0.0, elem)));
      }
      return fir::ValueR{ctx->get_constant_value(std::move(members), vty)};
    };
    // the address of part p of an access that starts at ptr
    auto part_addr = [&](fir::ValueR ptr, u32 p, u32 lanes, u32 elem_bytes) {
      if (p == 0) {
        return ptr;
      }
      return b.build_ptr_add(
          ptr, cint(static_cast<i128>(p) * lanes * elem_bytes, 64), false, true,
          true);
    };
    // lanes [offset, offset + len) of v, which has lanes lanes, the halves of
    // a vector are made once
    struct Halves {
      std::optional<fir::ValueR> low;
      std::optional<fir::ValueR> high;
    };
    TMap<fir::ValueR, Halves> halves;
    auto slice = [&](fir::ValueR v, u32 lanes, u32 offset, u32 len,
                     fir::TypeR elem) {
      while (lanes > len) {
        const u32 half = lanes / 2;
        const auto hty = vec_ty(elem, half);
        auto &h = halves[v];
        if (offset < half) {
          if (!h.low) {
            h.low = b.build_vector_op(v, hty, fir::VectorISubType::ExtractLow);
          }
          v = *h.low;
        } else {
          if (!h.high) {
            h.high =
                b.build_vector_op(v, hty, fir::VectorISubType::ExtractHigh);
          }
          v = *h.high;
          offset -= half;
        }
        lanes = half;
      }
      return v;
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
      const u32 lanes = pl[k];
      const u32 n_parts = lanes == 0 ? 0 : vf / lanes;
      if (n.kind == NodeKind::Phi) {
        // the value of the edge that was taken, the last edge is the default
        const auto &in = l.blocks[n.block].in_edges;
        auto edge_value = [&](const LoopEdge &e) {
          auto et = l.blocks[e.from].bb->get_terminator();
          return operand(et->bbs[e.slot].args[n.arg_id], lanes);
        };
        Parts acc = edge_value(in.back());
        for (size_t i = in.size() - 1; i > 0; i--) {
          const Parts m = edge_mask(in[i - 1]);
          const Parts v = edge_value(in[i - 1]);
          for (u32 p = 0; p < n_parts; p++) {
            acc[p] = b.build_select(vec_ty(n.type, lanes), m[p], v[p], acc[p]);
          }
        }
        vp[n.value] = acc;
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
          const auto ptr = vm.at(instr->args[0]);
          Parts res;
          for (u32 p = 0; p < n_parts; p++) {
            const auto addr = part_addr(ptr, p, lanes, n.type->get_size());
            if (mask) {
              res.push_back(b.build_intrinsic(
                  addr, (*mask)[p], zero_vec(n.type, lanes),
                  fir::IntrinsicSubType::MaskedLoad, vec_ty(n.type, lanes)));
              continue;
            }
            const fir::ValueR ops[] = {addr};
            res.push_back(emit_node(ctx, b, n, lanes, ops));
          }
          vp[n.value] = res;
          break;
        }
        case NodeKind::Store: {
          const auto ptr = vm.at(instr->args[0]);
          const auto val_it = l.node_of.find(instr->args[1]);
          const u32 st_lanes =
              val_it != l.node_of.end() &&
                      is_vector_shape(plan.nodes[val_it->second].shape)
                  ? pl[val_it->second]
                  : lanes;
          const Parts vals = operand(instr->args[1], st_lanes);
          for (u32 p = 0; p < vals.size(); p++) {
            const auto addr = part_addr(ptr, p, st_lanes, n.type->get_size());
            if (mask) {
              b.build_intrinsic(addr, (*mask)[p], vals[p],
                                fir::IntrinsicSubType::MaskedStore,
                                ctx->get_void_type());
              continue;
            }
            const fir::ValueR ops[] = {addr, vals[p]};
            emit_node(ctx, b, n, st_lanes, ops);
          }
          break;
        }
        case NodeKind::Widen: {
          const auto src = instr->args[0];
          const u32 j = l.node_of.at(src);
          const Parts &sp = vp.at(src);
          const u32 spl = pl[j];
          Parts res;
          for (u32 d = 0; d < n_parts; d++) {
            fir::ValueR v = sp[d];
            if (spl != lanes) {
              const u32 ratio = spl / lanes;
              v = slice(sp[d / ratio], spl, (d % ratio) * lanes, lanes,
                        n.op_type);
            }
            const fir::ValueR ops[] = {v};
            res.push_back(emit_node(ctx, b, n, lanes, ops));
          }
          vp[n.value] = res;
          break;
        }
        case NodeKind::Binary:
        case NodeKind::Cmp:
        case NodeKind::Select:
        case NodeKind::Zext:
        case NodeKind::ITrunc:
        case NodeKind::Unary:
        case NodeKind::Intrin: {
          TVec<Parts> args;
          for (auto a : instr->args) {
            args.push_back(operand(a, lanes));
          }
          Parts res;
          for (u32 p = 0; p < n_parts; p++) {
            TVec<fir::ValueR> ops;
            for (const auto &a : args) {
              ops.push_back(a[p]);
            }
            res.push_back(emit_node(ctx, b, n, lanes, ops));
          }
          vp[n.value] = res;
          break;
        }
        default:
          UNREACH();
      }
    }
    for (u32 k = 0; k < n_args; k++) {
      const auto &n = plan.nodes[k];
      if (n.kind == NodeKind::Induction && n.type->is_ptr()) {
        next_vals[k].push_back(ptr_add_like_step(
            b, k, vec_args[k][0], cint(static_cast<i128>(vf) * n.stride, 64)));
      } else if (n.kind == NodeKind::Induction) {
        next_vals[k].push_back(b.build_int_add(
            vec_args[k][0],
            cint(static_cast<i128>(vf) * n.stride, n.type->get_bitwidth())));
      } else {
        next_vals[k] = vp.at(back_args[k]);
      }
    }
    auto cond = b.build_int_cmp(next_vals[l.counter][0], end_counter,
                                fir::ICmpInstrSubType::NE);
    auto br = b.build_cond_branch(cond, vloop, vmid);
    for (u32 k = 0; k < n_args; k++) {
      for (auto v : next_vals[k]) {
        br.add_bb_arg(0, v);
        br.add_bb_arg(1, v);
      }
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
        finals.push_back(mid_args[k][0]);
        continue;
      }
      // the registers of the accumulator are combined first
      fir::ValueR acc = mid_args[k][0];
      for (size_t p = 1; p < mid_args[k].size(); p++) {
        acc = reduction_combine(b, n, acc, mid_args[k][p]);
      }
      auto red = b.build_reduce(acc, reduction_horizontal(n));
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