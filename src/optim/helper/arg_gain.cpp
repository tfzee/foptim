#include "arg_gain.hpp"

#include <optional>

#include "ir/basic_block.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/helper/helper.hpp"
#include "utils/map.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

namespace {
using Kind = ArgFact::Kind;
using fir::BinaryInstrSubType;
using fir::ICmpInstrSubType;
using fir::InstrType;
using fir::UnaryInstrSubType;

constexpr u32 MaxBits = 64;

u64 mask_of(u32 bits) { return bits >= 64 ? ~u64{0} : (u64{1} << bits) - 1; }

i64 sext(u64 v, u32 bits) {
  if (bits >= 64) {
    return static_cast<i64>(v);
  }
  const u32 sh = 64 - bits;
  return static_cast<i64>(v << sh) >> sh;
}

std::optional<ArgFact> fold_binary(BinaryInstrSubType op, const ArgFact &a,
                                   const ArgFact &b) {
  const u32 bits = a.bits;
  const u64 m = mask_of(bits);
  const i64 sa = sext(a.val, bits);
  const i64 sb = sext(b.val, bits);
  switch (op) {
    case BinaryInstrSubType::IntAdd:
      return ArgFact::int_(bits, a.val + b.val);
    case BinaryInstrSubType::IntSub:
      return ArgFact::int_(bits, a.val - b.val);
    case BinaryInstrSubType::IntMul:
      return ArgFact::int_(bits, a.val * b.val);
    case BinaryInstrSubType::And:
      return ArgFact::int_(bits, a.val & b.val);
    case BinaryInstrSubType::Or:
      return ArgFact::int_(bits, a.val | b.val);
    case BinaryInstrSubType::Xor:
      return ArgFact::int_(bits, a.val ^ b.val);
    case BinaryInstrSubType::Shl:
      if (b.val >= bits) {
        return std::nullopt;
      }
      return ArgFact::int_(bits, (a.val << b.val) & m);
    case BinaryInstrSubType::Shr:
      if (b.val >= bits) {
        return std::nullopt;
      }
      return ArgFact::int_(bits, a.val >> b.val);
    case BinaryInstrSubType::AShr:
      if (b.val >= bits) {
        return std::nullopt;
      }
      return ArgFact::int_(bits, static_cast<u64>(sa >> b.val));
    case BinaryInstrSubType::IntUDiv:
      if (b.val == 0) {
        return std::nullopt;
      }
      return ArgFact::int_(bits, a.val / b.val);
    case BinaryInstrSubType::IntURem:
      if (b.val == 0) {
        return std::nullopt;
      }
      return ArgFact::int_(bits, a.val % b.val);
    case BinaryInstrSubType::IntSDiv:
    case BinaryInstrSubType::IntSRem: {
      if (b.val == 0 || (sb == -1 && sa == sext(u64{1} << (bits - 1), bits))) {
        return std::nullopt;
      }
      return ArgFact::int_(
          bits, static_cast<u64>(op == BinaryInstrSubType::IntSDiv ? sa / sb
                                                                   : sa % sb));
    }
    default:
      return std::nullopt;
  }
}

std::optional<bool> fold_icmp(ICmpInstrSubType op, const ArgFact &a,
                              const ArgFact &b) {
  if (a.kind == Kind::Int && b.kind == Kind::Int) {
    const i64 sa = sext(a.val, a.bits);
    const i64 sb = sext(b.val, b.bits);
    switch (op) {
      case ICmpInstrSubType::EQ:
        return a.val == b.val;
      case ICmpInstrSubType::NE:
        return a.val != b.val;
      case ICmpInstrSubType::SLT:
        return sa < sb;
      case ICmpInstrSubType::SLE:
        return sa <= sb;
      case ICmpInstrSubType::SGT:
        return sa > sb;
      case ICmpInstrSubType::SGE:
        return sa >= sb;
      case ICmpInstrSubType::ULT:
        return a.val < b.val;
      case ICmpInstrSubType::ULE:
        return a.val <= b.val;
      case ICmpInstrSubType::UGT:
        return a.val > b.val;
      case ICmpInstrSubType::UGE:
        return a.val >= b.val;
      case ICmpInstrSubType::INVALID:
        return std::nullopt;
    }
    return std::nullopt;
  }
  if (op != ICmpInstrSubType::EQ && op != ICmpInstrSubType::NE) {
    return std::nullopt;
  }
  const bool eq = op == ICmpInstrSubType::EQ;
  if (a.kind == Kind::Null && b.kind == Kind::Null) {
    return eq;
  }
  if ((a.kind == Kind::Null && b.kind == Kind::NonNull) ||
      (a.kind == Kind::NonNull && b.kind == Kind::Null)) {
    return !eq;
  }
  return std::nullopt;
}

class Walker {
  fir::Function &f_;
  CFG &cfg_;
  TMap<fir::ValueR, ArgFact> vals_;

 public:
  Walker(fir::Function &f, CFG &cfg) : f_(f), cfg_(cfg) {}

  void set(fir::ValueR v, const ArgFact &fact) {
    if (fact.is_known()) {
      vals_[v] = fact;
    }
  }

  ArgFact get(fir::ValueR v) const {
    if (v.is_constant()) {
      auto c = v.as_constant();
      if (c->is_int()) {
        const u32 bits = v.get_type()->as_int();
        if (bits > MaxBits) {
          return {};
        }
        return ArgFact::int_(static_cast<u8>(bits),
                             static_cast<u64>(c->as_int()));
      }
      if (c->is_null()) {
        return ArgFact::null();
      }
      if (c->is_global() || c->is_func()) {
        return ArgFact::nonnull();
      }
      return {};
    }
    auto it = vals_.find(v);
    return it == vals_.end() ? ArgFact{} : it->second;
  }

  struct Eval {
    ArgFact val;
    bool vanishes = false;
  };

  Eval eval(fir::Instr instr) const {
    auto done = [](const ArgFact &v) { return Eval{v, v.is_const()}; };
    switch (instr->instr_type) {
      case InstrType::BinaryInstr: {
        const auto op = static_cast<BinaryInstrSubType>(instr->subtype);
        const ArgFact a = get(instr->args[0]);
        const ArgFact b = get(instr->args[1]);
        if (op == BinaryInstrSubType::PtrAdd) {
          if (b.kind == Kind::Int && b.val == 0) {
            return {a, true};
          }
          if (a.kind == Kind::NonNull && instr->InBounds) {
            return {ArgFact::nonnull(), false};
          }
          return {};
        }
        if (a.kind != Kind::Int || b.kind != Kind::Int) {
          return {};
        }
        auto r = fold_binary(op, a, b);
        return r ? done(*r) : Eval{};
      }
      case InstrType::ICmp: {
        auto r = fold_icmp(static_cast<ICmpInstrSubType>(instr->subtype),
                           get(instr->args[0]), get(instr->args[1]));
        return r ? done(ArgFact::int_(1, *r)) : Eval{};
      }
      case InstrType::UnaryInstr: {
        const ArgFact a = get(instr->args[0]);
        if (a.kind != Kind::Int) {
          return {};
        }
        switch (static_cast<UnaryInstrSubType>(instr->subtype)) {
          case UnaryInstrSubType::Not:
            return done(ArgFact::int_(a.bits, ~a.val));
          case UnaryInstrSubType::IntNeg:
            return done(ArgFact::int_(a.bits, u64{0} - a.val));
          default:
            return {};
        }
      }
      case InstrType::ITrunc:
      case InstrType::ZExt:
      case InstrType::SExt: {
        const ArgFact a = get(instr->args[0]);
        const auto ty = instr->result_type();
        if (a.kind != Kind::Int || !ty->is_int() || ty->as_int() > MaxBits) {
          return {};
        }
        const auto bits = static_cast<u8>(ty->as_int());
        const u64 v = instr->instr_type == InstrType::SExt
                          ? static_cast<u64>(sext(a.val, a.bits))
                          : a.val;
        return done(ArgFact::int_(bits, v));
      }
      case InstrType::SelectInstr: {
        const ArgFact c = get(instr->args[0]);
        if (c.kind != Kind::Int) {
          return {};
        }
        return {get(instr->args[c.val != 0 ? 1 : 2]), true};
      }
      default:
        return {};
    }
  }

  CFG &cfg() { return cfg_; }
  fir::Function &func() { return f_; }
};

}  // namespace

ArgFact ArgFact::int_(u8 bits, u64 val) {
  return {Kind::Int, bits, val & mask_of(bits)};
}

void facts_of_call(fir::Instr call, std::span<ArgFact> out) {
  ASSERT(out.size() + 1 == call->args.size());
  for (size_t i = 0; i < out.size(); i++) {
    const auto arg = call->args[i + 1];
    out[i] = ArgFact::unknown();
    if (arg.is_constant()) {
      auto c = arg.as_constant();
      if (c->is_int()) {
        const u32 bits = arg.get_type()->as_int();
        if (bits <= MaxBits) {
          out[i] = ArgFact::int_(static_cast<u8>(bits),
                                 static_cast<u64>(c->as_int()));
        }
      } else if (c->is_null()) {
        out[i] = ArgFact::null();
      } else if (c->is_global() || c->is_func()) {
        out[i] = ArgFact::nonnull();
      }
    } else if (arg.get_type()->is_ptr() && is_known_nonnull(arg)) {
      out[i] = ArgFact::nonnull();
    }
  }
}

ArgGain estimate_arg_gain(fir::Function &callee, std::span<const ArgFact> facts,
                          u32 budget) {
  ArgGain res;
  if (callee.is_decl()) {
    res.remaining = 0;
    return res;
  }
  CFG &cfg = AnalysisManager::cfg(callee);
  const u32 n_bbs = static_cast<u32>(cfg.bbrs.size());
  Walker w{callee, cfg};

  TVec<u32> rpo;
  rpo.reserve(n_bbs);
  TVec<u32> pos(n_bbs, ~0U);
  {
    TVec<u8> visited(n_bbs, 0);
    TVec<std::pair<u32, u32>> stack;
    stack.emplace_back(cfg.entry, 0);
    visited[cfg.entry] = 1;
    while (!stack.empty()) {
      auto &[node, next] = stack.back();
      const auto &succs = cfg.bbrs[node].succ;
      if (next < succs.size()) {
        const u32 s = succs[next++];
        if (!visited[s]) {
          visited[s] = 1;
          stack.emplace_back(s, 0);
        }
      } else {
        rpo.push_back(node);
        stack.pop_back();
      }
    }
    std::reverse(rpo.begin(), rpo.end());
    for (u32 i = 0; i < rpo.size(); i++) {
      pos[rpo[i]] = i;
    }
  }

  // joined edge values per block argument
  TVec<u32> arg_off(n_bbs + 1, 0);
  for (u32 b = 0; b < n_bbs; b++) {
    arg_off[b + 1] = arg_off[b] + static_cast<u32>(cfg.bbrs[b].bb->n_args());
  }
  TVec<ArgFact> join(arg_off[n_bbs]);
  TVec<u8> reached(n_bbs, 0);
  reached[cfg.entry] = 1;

  auto charge = [&](u32 n) {
    res.remaining += n;
    if (res.remaining > budget) {
      res.exceeded = true;
    }
  };

  for (const u32 b : rpo) {
    auto bb = cfg.bbrs[b].bb;
    if (!reached[b]) {
      res.dead += static_cast<u32>(bb->n_instrs());
      continue;
    }
    // not seen yet, a back edge may pass anything
    bool has_back_edge = false;
    for (const u32 p : cfg.bbrs[b].pred) {
      if (pos[p] != ~0U && pos[p] >= pos[b]) {
        has_back_edge = true;
      }
    }
    for (size_t i = 0; i < bb->n_args(); i++) {
      ArgFact v;
      if (b == cfg.entry) {
        if (i < facts.size() && !has_back_edge) {
          v = facts[i];
        }
        if (!v.is_known() && bb->args[i]->nonnull) {
          v = ArgFact::nonnull();
        }
      } else if (!has_back_edge) {
        v = join[arg_off[b] + i];
      }
      w.set(fir::ValueR{bb->args[i]}, v);
    }

    for (auto instr : bb->instructions) {
      const bool is_term = instr->is(InstrType::BranchInstr) ||
                           instr->is(InstrType::CondBranchInstr) ||
                           instr->is(InstrType::SwitchInstr) ||
                           instr->is(InstrType::ReturnInstr) ||
                           instr->is(InstrType::Unreachable);
      if (!is_term) {
        auto e = w.eval(instr);
        if (instr->has_one_result()) {
          w.set(instr.result(), e.val);
        }
        if (e.vanishes && !instr->has_pot_sideeffects()) {
          res.folded++;
        } else {
          charge(1);
        }
        continue;
      }
      size_t only = ~size_t{0};
      if (instr->is(InstrType::CondBranchInstr)) {
        const ArgFact c = w.get(instr->args[0]);
        if (c.kind == Kind::Int) {
          only = c.val != 0 ? 0 : 1;
          res.branches_folded++;
        } else {
          charge(1);
        }
      } else {
        charge(1);
      }
      const auto &targets = instr->get_bb_args();
      for (size_t k = 0; k < targets.size(); k++) {
        if (only != ~size_t{0} && k != only) {
          continue;
        }
        const u32 t = cfg.bbrs[b].succ[k];
        const auto &edge_args = targets[k].args;
        ASSERT(arg_off[t] + edge_args.size() == arg_off[t + 1]);
        for (size_t i = 0; i < edge_args.size(); i++) {
          const ArgFact v = w.get(edge_args[i]);
          auto &slot = join[arg_off[t] + i];
          if (!reached[t]) {
            slot = v;
          } else if (!(slot == v)) {
            slot = ArgFact::unknown();
          }
        }
        reached[t] = 1;
      }
    }
    if (res.exceeded) {
      return res;
    }
  }
  return res;
}

}  // namespace foptim::optim
