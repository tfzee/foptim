#include "optim/analysis/mem/provenance.hpp"

#include <optional>

#include "ir/basic_block.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "optim/analysis/mem/alloca_escape.hpp"

namespace foptim::optim {

bool Provenance::up_to_date(const fir::Function &func) const {
  return tab.parent_size() == func.ctx->alias_sets().size();
}

namespace {
using T = AliasSetTable;
constexpr u32 MaxRounds = 32;
constexpr u32 MaxDeriveDepth = 8;

// the callee of a direct call with a noalias result
const fir::Function *fresh_callee(fir::Instr instr) {
  if (!instr->is(fir::InstrType::CallInstr) || !instr->args[0].is_constant() ||
      !instr->args[0].as_constant()->is_func()) {
    return nullptr;
  }
  const auto *f = instr->args[0].as_constant()->as_func().func;
  return f->attribs.ret_noalias != 0 ? f : nullptr;
}

bool fresh_result(fir::Instr instr, u32 r) {
  const auto *f = fresh_callee(instr);
  return f != nullptr && f->result_noalias(r) && instr.result_type(r)->is_ptr();
}

// the set of base + index, a pointer computed from another one stays in its
// object (like LLVM, going outside of it is undefined behavior)
SetId ptr_add_set(AliasSetTable &tab, SetId base, bool in_bounds) {
  if (tab.subset(base, T::Null)) {
    return T::Unknown;
  }
  if (in_bounds) {
    return base;
  }
  // the sum may wrap around to null
  SetId r = T::Unknown;
  bool first = true;
  for (auto part : tab.is_union(base) ? FVec<SetId>(tab.parts(base).begin(),
                                                    tab.parts(base).end())
                                      : FVec<SetId>{base}) {
    auto p = part == T::IncomingNonNull ? T::Incoming : part;
    r = first ? p : tab.union_of(r, p);
    first = false;
  }
  return r;
}
}  // namespace

Provenance::Provenance(fir::Function &func) : tab(func.ctx->alias_sets()) {
  if (func.is_decl()) {
    return;
  }
  FDMap<fir::ValueR, SetId> load_of_alloca;
  for (const auto bb : func.basic_blocks) {
    for (const auto instr : bb->instructions) {
      const bool is_alloca = instr->is(fir::InstrType::AllocaInstr);
      if (!is_alloca && fresh_callee(instr) == nullptr) {
        continue;
      }
      for (u32 r = 0; r < instr.n_results(); r++) {
        if (!is_alloca && !fresh_result(instr, r)) {
          continue;
        }
        const auto res = instr.result(r);
        auto info = analyze_alloca_escape(res);
        const SetId foreign[] = {T::Foreign};
        const SetId root = is_alloca ? T::Stack : T::Heap;
        SetId obj = info.escapes ? tab.create_object(root, foreign)
                                 : tab.create_object(root);
        // an object that is disjoint from Foreign is disjoint from Null too, so
        // a heap object that may be null is a union with Null
        const bool maybe_null =
            !is_alloca && !fresh_callee(instr)->result_nonnull(r);
        if (!is_alloca && !maybe_null) {
          tab.declare_disjoint(obj, T::Null);
        }
        if (!info.escapes) {
          tab.declare_disjoint(obj, T::Foreign);
          if (info.has_self_ptr) {
            for (const auto &v : info.derived) {
              if (v.is_instr() && v.as_instr()->is(fir::InstrType::LoadInstr)) {
                load_of_alloca[v] = obj;
              }
            }
          }
        }
        sets[res] =
            maybe_null && !info.escapes ? tab.union_of(obj, T::Null) : obj;
        escaping[res] = info.escapes;
      }
    }
  }

  FDMap<fir::ValueR, FVec<fir::ValueR>> incoming;
  auto entry = func.get_entry();
  for (const auto bb : func.basic_blocks) {
    if (bb->instructions.empty()) {
      continue;
    }
    auto term = bb->instructions.back();
    for (const auto &target : term->bbs) {
      if (target.bb == entry) {
        continue;
      }
      for (size_t j = 0; j < target.args.size(); j++) {
        auto param = fir::ValueR{target.bb->args[j]};
        if (param.get_type()->is_ptr()) {
          incoming[param].push_back(target.args[j]);
        }
      }
    }
  }

  for (auto arg : entry->args) {
    if (!fir::ValueR{arg}.get_type()->is_ptr()) {
      continue;
    }
    if (arg->noalias) {
      auto obj = tab.create_object(T::Restrict);
      if (arg->nonnull) {
        tab.declare_disjoint(obj, T::Null);
      }
      sets[fir::ValueR{arg}] = obj;
    } else {
      sets[fir::ValueR{arg}] = arg->nonnull ? T::IncomingNonNull : T::Incoming;
    }
  }

  auto operand = [&](fir::ValueR v) -> std::optional<SetId> {
    if (!v.get_type()->is_ptr()) {
      return T::Unknown;
    }
    if (v.is_constant()) {
      return constant_set(v);
    }
    auto it = sets.find(v);
    if (it == sets.end()) {
      return std::nullopt;
    }
    return it->second;
  };

  auto transfer = [&](fir::Instr instr) -> std::optional<SetId> {
    if (instr->is(fir::BinaryInstrSubType::PtrAdd)) {
      auto base = operand(instr->args[0]);
      if (!base) {
        return std::nullopt;
      }
      return ptr_add_set(tab, *base, instr->InBounds);
    }
    if (instr->is(fir::InstrType::SelectInstr)) {
      auto a = operand(instr->args[1]);
      auto b = operand(instr->args[2]);
      if (a && b) {
        return tab.union_of(*a, *b);
      }
      return a ? a : b;
    }
    if (instr->is(fir::InstrType::LoadInstr)) {
      auto it = load_of_alloca.find(instr.result());
      return it == load_of_alloca.end() ? T::Foreign
                                        : tab.union_of(T::Foreign, it->second);
    }
    if (instr->is(fir::InstrType::CallInstr)) {
      return T::Foreign;
    }
    if (instr->is(fir::ConversionSubType::BitCast)) {
      return operand(instr->args[0]);
    }
    return T::Unknown;
  };

  for (u32 round = 0;; round++) {
    bool changed = false;
    auto update = [&](fir::ValueR v, std::optional<SetId> n) {
      if (!n) {
        return;
      }
      auto it = sets.find(v);
      if (it == sets.end()) {
        sets[v] = round >= MaxRounds ? T::Unknown : *n;
        changed = true;
        return;
      }
      const SetId joined =
          round >= MaxRounds ? T::Unknown : tab.union_of(it->second, *n);
      if (joined != it->second) {
        it->second = joined;
        changed = true;
      }
    };
    for (const auto bb : func.basic_blocks) {
      if (bb != entry) {
        for (auto arg : bb->args) {
          auto param = fir::ValueR{arg};
          if (!param.get_type()->is_ptr()) {
            continue;
          }
          std::optional<SetId> n;
          for (auto in : incoming[param]) {
            auto s = operand(in);
            if (s) {
              n = n ? tab.union_of(*n, *s) : *s;
            }
          }
          update(param, n);
        }
      }
      for (const auto instr : bb->instructions) {
        if (instr->is(fir::InstrType::AllocaInstr)) {
          continue;
        }
        const u32 n_res = instr.n_results();
        for (u32 r = 0; r < n_res; r++) {
          if (!instr.result_type(r)->is_ptr() || fresh_result(instr, r)) {
            continue;
          }
          update(instr.result(r), n_res == 1
                                      ? transfer(instr)
                                      : std::optional<SetId>{T::Foreign});
        }
      }
    }
    if (!changed) {
      break;
    }
  }
}

SetId Provenance::constant_set(fir::ValueR v) {
  auto it = const_sets.find(v);
  if (it != const_sets.end()) {
    return it->second;
  }
  SetId s = T::Unknown;
  auto c = v.as_constant();
  if (c->is_null() || c->is_poison()) {
    s = T::Null;
  } else if (c->is_global()) {
    const SetId ro[] = {T::ReadOnlyGlobals};
    if (c->as_global()->is_constant) {
      s = tab.create_object(T::Globals, ro);
    } else if (c->as_global()->addr_private) {
      s = tab.create_object(T::PrivateGlobals);
    } else {
      s = tab.create_object(T::Globals);
    }
  } else if (c->is_func()) {
    s = c->as_func()->is_decl() ? T::Foreign : tab.create_object(T::Globals);
  }
  const_sets[v] = s;
  return s;
}

SetId Provenance::set_of(fir::ValueR v) {
  if (!v.get_type()->is_ptr()) {
    return T::Unknown;
  }
  if (v.is_constant()) {
    return constant_set(v);
  }
  auto it = sets.find(v);
  return it == sets.end() ? derive(v, 0) : it->second;
}

SetId Provenance::derive(fir::ValueR v, u32 depth) {
  if (!v.get_type()->is_ptr()) {
    return T::Unknown;
  }
  if (v.is_constant()) {
    return constant_set(v);
  }
  auto it = sets.find(v);
  if (it != sets.end()) {
    return it->second;
  }
  if (!v.is_instr() || depth >= MaxDeriveDepth) {
    return T::Unknown;
  }
  auto instr = v.as_instr();
  if (instr.n_results() != 1) {
    return T::Unknown;
  }
  if (instr->is(fir::BinaryInstrSubType::PtrAdd)) {
    return ptr_add_set(tab, derive(instr->args[0], depth + 1), instr->InBounds);
  }
  if (instr->is(fir::InstrType::SelectInstr)) {
    return tab.union_of(derive(instr->args[1], depth + 1),
                        derive(instr->args[2], depth + 1));
  }
  if (instr->is(fir::ConversionSubType::BitCast)) {
    return derive(instr->args[0], depth + 1);
  }
  if (instr->is(fir::InstrType::CallInstr)) {
    return T::Foreign;
  }
  // new loads may read a pointer back out of an alloca, so they stay unknown
  return T::Unknown;
}

bool Provenance::is_heap(fir::ValueR v) {
  const SetId s = set_of(v);
  if (!tab.is_union(s)) {
    return tab.subset(s, T::Heap);
  }
  bool any = false;
  for (auto p : tab.parts(s)) {
    if (tab.subset(p, T::Heap)) {
      any = true;
    } else if (!tab.subset(p, T::Null)) {
      return false;
    }
  }
  return any;
}

bool Provenance::escapes(fir::ValueR alloca) const {
  auto it = escaping.find(alloca);
  return it != escaping.end() && it->second;
}

bool Provenance::equivalent(const Provenance &o) const {
  if (sets.size() != o.sets.size()) {
    return false;
  }
  for (const auto &[v, s] : sets) {
    auto it = o.sets.find(v);
    if (it == o.sets.end() || it->second != s) {
      return false;
    }
  }
  return true;
}

}  // namespace foptim::optim
