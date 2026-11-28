#include "narrow_ints.hpp"

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "utils/map.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

namespace {
using fir::BinaryInstrSubType;
using fir::InstrType;
using fir::UnaryInstrSubType;
using fir::UseType;
using fir::ValueR;

enum class Kind : u8 { Arith, Unary, Ext, Trunc, Other };

// where a value flows into a block argument
struct Incoming {
  fir::Instr term;
  u16 bb_id;
  u16 arg_id;
};

u32 width_of(ValueR v) { return v.get_type()->as_int(); }

bool is_scalar_int(ValueR v) { return v.get_type()->is_int(); }

class Narrower {
  fir::Context &ctx;
  fir::Function &func;

  TMap<ValueR, u32> dem;
  TMap<ValueR, TVec<Incoming>> incoming;
  TVec<ValueR> tracked_list;
  TVec<ValueR> work;
  TSet<ValueR> in_n;
  TVec<ValueR> members;
  TMap<ValueR, ValueR> cast_repl;
  TMap<ValueR, TVec<std::pair<u32, ValueR>>> trunc_memo;

  static Kind kind_of(fir::Instr i) {
    if (!i->has_one_result() || !i.result_type()->is_int()) {
      return Kind::Other;
    }
    switch (i->instr_type) {
      case InstrType::BinaryInstr:
        switch (static_cast<BinaryInstrSubType>(i->subtype)) {
          case BinaryInstrSubType::IntAdd:
          case BinaryInstrSubType::IntSub:
          case BinaryInstrSubType::IntMul:
          case BinaryInstrSubType::And:
          case BinaryInstrSubType::Or:
          case BinaryInstrSubType::Xor:
            return Kind::Arith;
          default:
            return Kind::Other;
        }
      case InstrType::UnaryInstr:
        switch (static_cast<UnaryInstrSubType>(i->subtype)) {
          case UnaryInstrSubType::IntNeg:
          case UnaryInstrSubType::Not:
            return Kind::Unary;
          default:
            return Kind::Other;
        }
      case InstrType::ZExt:
      case InstrType::SExt:
        return Kind::Ext;
      case InstrType::ITrunc:
        return Kind::Trunc;
      default:
        return Kind::Other;
    }
  }

  [[nodiscard]] bool tracked(ValueR v) const {
    if (v.is_bb_arg()) {
      if (v.as_bb_arg()->get_parent() == func.get_entry()) {
        return false;
      }
    } else if (!v.is_instr() || !v.as_instr()->has_one_result()) {
      return false;
    }
    if (!is_scalar_int(v)) {
      return false;
    }
    const u32 w = width_of(v);
    return w > 8 && w <= 64;
  }

  static u32 norm(u32 c) {
    if (c == 0) {
      return 0;
    }
    if (c <= 8) {
      return 8;
    }
    if (c <= 16) {
      return 16;
    }
    if (c <= 32) {
      return 32;
    }
    return 64;
  }

  void raise(ValueR op, u32 c) {
    if (!tracked(op)) {
      return;
    }
    c = std::min(norm(std::min(c, width_of(op))), width_of(op));
    auto [it, inserted] = dem.try_emplace(op, 0);
    if (c > it->second) {
      it->second = c;
      work.push_back(op);
    }
  }

  void raise_full(ValueR op) {
    if (is_scalar_int(op)) {
      raise(op, width_of(op));
    }
  }

  void flow(fir::Instr u, u32 d) {
    switch (kind_of(u)) {
      case Kind::Arith:
      case Kind::Unary:
      case Kind::Trunc:
        for (auto a : u->args) {
          raise(a, d);
        }
        break;
      case Kind::Ext:
        raise(u->args[0], std::min(d, width_of(u->args[0])));
        break;
      case Kind::Other:
        for (auto a : u->args) {
          raise_full(a);
        }
        break;
    }
  }

  [[nodiscard]] u32 dem_of(ValueR v) const {
    auto it = dem.find(v);
    return it == dem.end() ? 0 : it->second;
  }

  // width of the value once all accepted rewrites are done
  [[nodiscard]] u32 eff_width(ValueR v) const {
    if (in_n.contains(v)) {
      return dem_of(v);
    }
    return width_of(v);
  }

  void collect() {
    for (auto bb : func.get_bbs()) {
      for (auto arg : bb->args) {
        ValueR a{arg};
        if (tracked(a)) {
          tracked_list.push_back(a);
        }
      }
      for (auto instr : bb->get_instrs()) {
        if (instr->has_one_result() && tracked(instr.result())) {
          tracked_list.push_back(instr.result());
        }
        for (size_t bi = 0; bi < instr->bbs.size(); bi++) {
          auto &target = instr->bbs[bi];
          for (size_t j = 0;
               j < target.args.size() && j < target.bb->args.size(); j++) {
            auto it =
                incoming
                    .try_emplace(ValueR{target.bb->args[j]}, TVec<Incoming>{})
                    .first;
            it->second.push_back(Incoming{.term = instr,
                                          .bb_id = static_cast<u16>(bi),
                                          .arg_id = static_cast<u16>(j)});
          }
        }
      }
    }
  }

  void analyse() {
    for (auto bb : func.get_bbs()) {
      for (auto instr : bb->get_instrs()) {
        const bool tr = instr->has_one_result() && tracked(instr.result());
        const Kind k = kind_of(instr);
        if (k == Kind::Other) {
          flow(instr, 0);
        } else if (!tr) {
          // result not tracked, it uses all of its width
          flow(instr, instr->has_one_result() && is_scalar_int(instr.result())
                          ? width_of(instr.result())
                          : 0);
        }
      }
    }
    while (!work.empty()) {
      ValueR v = work.back();
      work.pop_back();
      const u32 d = dem_of(v);
      if (v.is_instr()) {
        flow(v.as_instr(), d);
      } else if (auto it = incoming.find(v); it != incoming.end()) {
        for (auto &in : it->second) {
          raise(in.term->bbs[in.bb_id].args[in.arg_id], d);
        }
      }
    }
  }

  [[nodiscard]] bool candidate(ValueR v) const {
    const u32 d = dem_of(v);
    if (d == 0 || d >= width_of(v)) {
      return false;
    }
    return v.is_bb_arg() || kind_of(v.as_instr()) != Kind::Other;
  }

  [[nodiscard]] bool user_ok(ValueR v) const {
    for (auto &use : *v.get_uses()) {
      if (use.type == UseType::NormalArg) {
        if (use.user->is(InstrType::ITrunc)) {
          continue;
        }
        if (use.user->has_one_result() && in_n.contains(use.user.result())) {
          continue;
        }
        return false;
      }
      if (use.type == UseType::BBArg) {
        auto tgt = use.user->bbs[use.argId].bb->args[use.bbArgId];
        if (!in_n.contains(ValueR{tgt})) {
          return false;
        }
      }
    }
    return true;
  }

  // drops values that have a user which is neither inside the set nor a trunc
  void close_set() {
    TVec<ValueR> check(members.begin(), members.end());
    while (!check.empty()) {
      ValueR v = check.back();
      check.pop_back();
      if (!in_n.contains(v) || user_ok(v)) {
        continue;
      }
      in_n.erase(v);
      if (v.is_instr()) {
        for (auto a : v.as_instr()->args) {
          if (in_n.contains(a)) {
            check.push_back(a);
          }
        }
      } else if (auto it = incoming.find(v); it != incoming.end()) {
        for (auto &in : it->second) {
          auto op = in.term->bbs[in.bb_id].args[in.arg_id];
          if (in_n.contains(op)) {
            check.push_back(op);
          }
        }
      }
    }
  }

  // the operands of a member that are members too
  template <class F>
  void for_each_operand(ValueR v, F &&f) {
    if (v.is_instr()) {
      for (auto a : v.as_instr()->args) {
        f(a);
      }
    } else if (auto it = incoming.find(v); it != incoming.end()) {
      for (auto &in : it->second) {
        f(in.term->bbs[in.bb_id].args[in.arg_id]);
      }
    }
  }

  // components that remove more instructions than they add
  void filter_components() {
    TMap<ValueR, u32> id;
    TVec<u32> parent;
    for (auto v : members) {
      if (in_n.contains(v)) {
        id.emplace(v, static_cast<u32>(parent.size()));
        parent.push_back(static_cast<u32>(parent.size()));
      }
    }
    auto find = [&](u32 x) {
      while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
      }
      return x;
    };
    for (auto v : members) {
      if (!in_n.contains(v)) {
        continue;
      }
      for_each_operand(v, [&](ValueR op) {
        auto it = id.find(op);
        if (it != id.end() && in_n.contains(op)) {
          parent[find(id.at(v))] = find(it->second);
        }
      });
    }
    TVec<i32> gain(parent.size(), 0);
    TVec<i32> loss(parent.size(), 0);
    TMap<ValueR, TVec<std::pair<u32, u32>>> seen;
    auto adjust_cost = [&](u32 comp, ValueR op, u32 d) {
      if (op.is_constant() || eff_width(op) == d) {
        return;
      }
      auto &vec =
          seen.try_emplace(op, TVec<std::pair<u32, u32>>{}).first->second;
      for (auto &p : vec) {
        if (p.first == comp && p.second == d) {
          return;
        }
      }
      vec.emplace_back(comp, d);
      loss[comp]++;
    };
    for (auto v : members) {
      if (!in_n.contains(v)) {
        continue;
      }
      const u32 comp = find(id.at(v));
      const u32 d = dem_of(v);
      if (v.is_instr()) {
        const Kind k = kind_of(v.as_instr());
        if (k == Kind::Ext || k == Kind::Trunc) {
          gain[comp]++;
        }
      }
      for_each_operand(v, [&](ValueR op) { adjust_cost(comp, op, d); });
      for (auto &use : *v.get_uses()) {
        if (use.type == UseType::NormalArg && use.user->is(InstrType::ITrunc) &&
            !in_n.contains(use.user.result()) &&
            eff_width(v) == width_of(use.user.result())) {
          gain[comp]++;
        }
      }
    }
    for (auto v : members) {
      if (in_n.contains(v) && gain[find(id.at(v))] <= loss[find(id.at(v))]) {
        in_n.erase(v);
      }
    }
  }

  fir::TypeR int_ty(u32 w) { return ctx->get_int_type(static_cast<u16>(w)); }

  ValueR const_adjust(ValueR c, u32 d, bool is_signed) {
    const APInt &a = c.as_constant()->as_apint();
    if (a.bits() >= d) {
      return ValueR{ctx->get_constant_int(a.trunc(d))};
    }
    return ValueR{ctx->get_constant_int(is_signed ? a.sext(d) : a.zext(d))};
  }

  fir::Builder after_def(ValueR v) {
    if (v.is_instr()) {
      fir::Builder b{v.as_instr()};
      b.after(v.as_instr());
      return b;
    }
    fir::Builder b{v.as_bb_arg()->get_parent()};
    b.at_start(v.as_bb_arg()->get_parent());
    return b;
  }

  // v with the width d, widening keeps the sign of is_signed
  ValueR adjust(ValueR v, u32 d, bool is_signed) {
    if (v.is_constant()) {
      return const_adjust(v, d, is_signed);
    }
    const u32 cw = width_of(v);
    if (cw == d) {
      return v;
    }
    if (cw > d) {
      auto [it, inserted] =
          trunc_memo.try_emplace(v, TVec<std::pair<u32, ValueR>>{});
      for (auto &p : it->second) {
        if (p.first == d) {
          return p.second;
        }
      }
      auto b = after_def(v);
      ValueR r = b.build_itrunc(v, int_ty(d));
      it->second.emplace_back(d, r);
      return r;
    }
    auto b = after_def(v);
    return is_signed ? b.build_sext(v, int_ty(d)) : b.build_zext(v, int_ty(d));
  }

  [[nodiscard]] bool is_cast_member(ValueR v) const {
    if (!v.is_instr() || !in_n.contains(v)) {
      return false;
    }
    const Kind k = kind_of(v.as_instr());
    return k == Kind::Ext || k == Kind::Trunc;
  }

  // the value that stands in for v after the rewrite
  ValueR get(ValueR v) {
    if (!is_cast_member(v)) {
      return v;
    }
    if (auto it = cast_repl.find(v); it != cast_repl.end()) {
      return it->second;
    }
    auto i = v.as_instr();
    ValueR src = get(i->args[0]);
    ValueR r = adjust(src, dem_of(v), i->is(InstrType::SExt));
    cast_repl.emplace(v, r);
    return r;
  }

  void rewrite() {
    TVec<ValueR> list;
    for (auto v : members) {
      if (in_n.contains(v)) {
        list.push_back(v);
      }
    }
    // every trunc that reads a member and is not one itself
    TVec<fir::Instr> truncs;
    for (auto v : list) {
      for (auto &use : *v.get_uses()) {
        if (use.type == UseType::NormalArg && use.user->is(InstrType::ITrunc) &&
            !in_n.contains(use.user.result())) {
          truncs.push_back(use.user);
        }
      }
    }
    for (auto v : list) {
      const u32 d = dem_of(v);
      if (v.is_bb_arg()) {
        v.as_bb_arg()->_type = int_ty(d);
      } else if (!is_cast_member(v)) {
        v.as_instr()->set_result_type(int_ty(d));
        v.as_instr()->clear_poison_flags();
      }
    }
    for (auto v : list) {
      const u32 d = dem_of(v);
      if (v.is_bb_arg()) {
        auto it = incoming.find(v);
        if (it == incoming.end()) {
          continue;
        }
        for (auto &in : it->second) {
          ValueR old = in.term->bbs[in.bb_id].args[in.arg_id];
          ValueR nv = adjust(get(old), d, true);
          if (!(nv == old)) {
            in.term.replace_bb_arg(in.bb_id, in.arg_id, nv);
          }
        }
      } else if (!is_cast_member(v)) {
        auto i = v.as_instr();
        for (u32 k = 0; k < i->args.size(); k++) {
          ValueR old = i->args[k];
          ValueR nv = adjust(get(old), d, true);
          if (!(nv == old)) {
            i.replace_arg(k, nv);
          }
        }
      }
    }
    TVec<fir::Instr> dead;
    for (auto v : list) {
      if (is_cast_member(v)) {
        ValueR r = get(v);
        v.replace_all_uses(r);
        dead.push_back(v.as_instr());
      }
    }
    for (auto t : truncs) {
      if (!t.is_valid() || in_n.contains(t.result())) {
        continue;
      }
      ValueR x = get(t->args[0]);
      if (width_of(x) == width_of(t.result())) {
        t.result().replace_all_uses(x);
        dead.push_back(t);
      } else if (width_of(x) < width_of(t.result())) {
        // the demand of the trunc is covered by the narrowed width, the
        // bits above are not read
        fir::Builder b{t};
        t.result().replace_all_uses(b.build_zext(x, t.result_type()));
        dead.push_back(t);
      } else if (!(x == t->args[0])) {
        t.replace_arg(0, x);
      }
    }
    for (auto i : dead) {
      i.destroy();
    }
  }

 public:
  Narrower(fir::Context &ctx, fir::Function &func) : ctx(ctx), func(func) {}

  bool run() {
    collect();
    analyse();
    for (auto v : tracked_list) {
      if (candidate(v)) {
        members.push_back(v);
        in_n.insert(v);
      }
    }
    if (members.empty()) {
      return false;
    }
    close_set();
    filter_components();
    close_set();
    bool any = false;
    for (auto v : members) {
      any |= in_n.contains(v);
    }
    if (!any) {
      return false;
    }
    rewrite();
    return true;
  }
};
}  // namespace

PreservedAnalysis NarrowInts::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("NarrowInts");
  Narrower n{ctx, func};
  return n.run() ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
