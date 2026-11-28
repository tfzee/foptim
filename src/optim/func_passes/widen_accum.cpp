#include "widen_accum.hpp"

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

namespace {
using fir::BinaryInstrSubType;
using fir::InstrType;
using fir::UseType;
using fir::ValueR;

bool low_bits_op(fir::Instr i) {
  if (!i->is(InstrType::BinaryInstr) || !i->has_one_result() ||
      !i.result_type()->is_int()) {
    return false;
  }
  switch (static_cast<BinaryInstrSubType>(i->subtype)) {
    case BinaryInstrSubType::IntAdd:
    case BinaryInstrSubType::IntSub:
    case BinaryInstrSubType::IntMul:
    case BinaryInstrSubType::And:
    case BinaryInstrSubType::Or:
    case BinaryInstrSubType::Xor:
      return true;
    default:
      return false;
  }
}

bool defined_outside(ValueR v, fir::BasicBlock bb) {
  if (v.is_instr()) {
    return v.as_instr()->get_parent() != bb;
  }
  return v.is_bb_arg() && v.as_bb_arg()->get_parent() != bb;
}

class Widener {
  fir::Context &ctx;
  u32 wide = 0;

  fir::TypeR int_ty(u32 w) { return ctx->get_int_type(static_cast<u16>(w)); }

  // the value is a trunc of a wider value, a constant, or an operation of
  // those only used here. wide becomes the widest source
  bool wide_tree(ValueR v, fir::BasicBlock bb) {
    if (v.is_constant_int()) {
      return true;
    }
    // an invariant value is extended buht only its low bits matter
    const bool outside = defined_outside(v, bb);
    if (outside && v.get_type()->is_int() &&
        v.get_type()->get_bitwidth() <= 64) {
      wide = std::max<u32>(wide, v.get_type()->get_bitwidth());
      return true;
    }
    if (!v.is_instr()) {
      return false;
    }
    auto i = v.as_instr();
    if (i->is(InstrType::ITrunc)) {
      const u32 w = i->args[0].get_type()->get_bitwidth();
      if (!i->args[0].get_type()->is_int() || w > 64) {
        return false;
      }
      wide = std::max(wide, w);
      return true;
    }
    return low_bits_op(i) && i->get_parent() == bb && v.get_n_uses() == 1 &&
           wide_tree(i->args[0], bb) && wide_tree(i->args[1], bb);
  }

  ValueR build_wide(ValueR v, fir::Builder &b, fir::BasicBlock bb) {
    if (v.is_constant_int()) {
      return ValueR{ctx->get_constant_int(v.as_constant()->as_int(), wide)};
    }
    const bool outside = defined_outside(v, bb);
    if (outside) {
      return v.get_type()->get_bitwidth() == wide
                 ? v
                 : b.build_zext(v, int_ty(wide));
    }
    auto i = v.as_instr();
    if (i->is(InstrType::ITrunc)) {
      return i->args[0].get_type()->get_bitwidth() == wide
                 ? i->args[0]
                 : b.build_zext(i->args[0], int_ty(wide));
    }
    ValueR x = build_wide(i->args[0], b, bb);
    ValueR y = build_wide(i->args[1], b, bb);
    return b.build_binary_op(x, y, static_cast<BinaryInstrSubType>(i->subtype));
  }

  // an operand can be shared inside the tree (x op x, or one operand inside
  // the other), so destroyed values gotta not be touched again
  static void kill_tree(ValueR root) {
    TVec<ValueR> work{root};
    TVec<fir::Instr> dead;
    while (!work.empty()) {
      ValueR v = work.back();
      work.pop_back();
      if (!v.is_instr()) {
        continue;
      }
      auto i = v.as_instr();
      if (std::ranges::find(dead, i) != dead.end() || v.get_n_uses() != 0) {
        continue;
      }
      if (i->is(InstrType::ITrunc) || !low_bits_op(i)) {
        if (i->is(InstrType::ITrunc)) {
          dead.push_back(i);
          i.destroy();
        }
        continue;
      }
      work.push_back(i->args[0]);
      work.push_back(i->args[1]);
      dead.push_back(i);
      i.destroy();
    }
  }

  bool widen_arg(fir::BasicBlock bb, u16 back_i, u16 k) {
    ValueR acc{bb->args[k]};
    if (!acc.get_type()->is_int() || acc.get_n_uses() != 1) {
      return false;
    }
    const u32 narrow = acc.get_type()->get_bitwidth();
    const auto &au = acc.get_uses()->front();
    if (au.type != UseType::NormalArg || au.user->get_parent() != bb ||
        !low_bits_op(au.user) ||
        (static_cast<BinaryInstrSubType>(au.user->subtype) ==
             BinaryInstrSubType::IntSub &&
         au.argId != 0)) {
      return false;
    }
    auto u = au.user;
    const u16 acc_pos = au.argId;
    const ValueR t = u->args[1 - acc_pos];
    auto term = bb->get_terminator();
    // the sum goes around the loop and out on exits. with one exit edge every
    // path to a use behind the loop passes it, so those uses read the trunc
    TVec<u16> exit_edges;
    TVec<fir::Use> outside;
    bool back_seen = false;
    for (const auto &use : *u.result().get_uses()) {
      if (use.user->get_parent() != bb) {
        outside.push_back(use);
        continue;
      }
      if (use.type != UseType::BBArg || use.user != term) {
        return false;
      }
      if (use.argId == back_i) {
        if (use.bbArgId != k || back_seen) {
          return false;
        }
        back_seen = true;
      } else if (std::ranges::find(exit_edges, use.argId) == exit_edges.end()) {
        exit_edges.push_back(use.argId);
      }
    }
    if (!back_seen || (!outside.empty() && term->bbs.size() != 2)) {
      return false;
    }
    if (!outside.empty()) {
      exit_edges = {static_cast<u16>(1 - back_i)};
    }
    wide = 0;
    if (!wide_tree(t, bb) || wide <= narrow) {
      return false;
    }
    TVec<std::pair<fir::Instr, u16>> entries;
    for (const auto &use : bb->uses) {
      if (use.type == UseType::BB && use.user != term) {
        entries.emplace_back(use.user, use.argId);
      }
    }
    if (entries.empty()) {
      return false;
    }

    fir::Builder b(u);
    ValueR y = build_wide(t, b, bb);
    acc.as_bb_arg()->_type = int_ty(wide);
    ValueR nu = acc_pos == 0
                    ? b.build_binary_op(
                          acc, y, static_cast<BinaryInstrSubType>(u->subtype))
                    : b.build_binary_op(
                          y, acc, static_cast<BinaryInstrSubType>(u->subtype));
    for (auto [pred, i] : entries) {
      ValueR v = pred->bbs[i].args[k];
      ValueR nv;
      if (v.is_constant_int()) {
        nv = ValueR{ctx->get_constant_int(v.as_constant()->as_int(), wide)};
      } else {
        fir::Builder pb(pred);
        nv = pb.build_zext(v, int_ty(wide));
      }
      pred.replace_bb_arg(i, k, nv);
    }
    term.replace_bb_arg(back_i, k, nu);
    ValueR old = u.result();
    for (u16 i : exit_edges) {
      TVec<ValueR> old_args{term->bbs[i].args.begin(), term->bbs[i].args.end()};
      auto target = term->bbs[i].bb;
      fir::Builder bld{target};
      auto n = bld.append_bb();
      bld.at_end(n);
      ValueR tr = bld.build_itrunc(nu, int_ty(narrow));
      auto br = bld.build_branch(target);
      for (auto a : old_args) {
        br.add_bb_arg(0, a == old ? tr : a);
      }
      term.replace_bb(i, n, false);
      for (auto use : outside) {
        use.replace_use(tr);
      }
    }
    u.destroy();
    kill_tree(t);
    return true;
  }

  // store(p, or(trunc a, trunc b)) stores trunc(or(a, b)), the vectorizer only
  // takes a trunc in front of a store
  bool sink_store_trunc(fir::Instr st, fir::BasicBlock bb) {
    ValueR v = st->args[1];
    if (!v.is_instr() || !low_bits_op(v.as_instr()) ||
        v.as_instr()->get_parent() != bb || v.get_n_uses() != 1) {
      return false;
    }
    wide = 0;
    if (!wide_tree(v, bb) || wide <= v.get_type()->get_bitwidth()) {
      return false;
    }
    fir::Builder b(st);
    ValueR y = build_wide(v, b, bb);
    ValueR t = b.build_itrunc(y, v.get_type());
    st.replace_arg(1, t);
    kill_tree(v);
    return true;
  }

 public:
  explicit Widener(fir::Context &ctx) : ctx(ctx) {}

  bool cfg_changed = false;

  bool run_block(fir::BasicBlock bb) {
    auto term = bb->get_terminator();
    if (!term->is(InstrType::CondBranchInstr) &&
        !term->is(InstrType::BranchInstr)) {
      return false;
    }
    u16 back_i = 0;
    u32 n_back = 0;
    for (u32 i = 0; i < term->bbs.size(); i++) {
      if (term->bbs[i].bb == bb) {
        back_i = i;
        n_back++;
      }
    }
    if (n_back != 1 || term->bbs.size() < 2) {
      return false;
    }
    bool changed = false;
    for (u32 k = 0; k < bb->args.size(); k++) {
      const bool w = widen_arg(bb, back_i, k);
      cfg_changed |= w;
      changed |= w;
    }
    TVec<fir::Instr> stores;
    for (auto instr : bb->instructions) {
      if (instr->is(InstrType::StoreInstr)) {
        stores.push_back(instr);
      }
    }
    for (auto st : stores) {
      changed |= sink_store_trunc(st, bb);
    }
    return changed;
  }
};
}  // namespace

PreservedAnalysis WidenAccum::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("WidenAccum");
  Widener w{ctx};
  TVec<fir::BasicBlock> bbs;
  for (auto bb : func.get_bbs()) {
    bbs.push_back(bb);
  }
  bool changed = false;
  for (auto bb : bbs) {
    changed |= w.run_block(bb);
  }
  if (!changed) {
    return PreservedAnalysis::all();
  }
  return w.cfg_changed ? PreservedAnalysis::none()
                       : PreservedAnalysis::cfg_only();
}

}  // namespace foptim::optim
