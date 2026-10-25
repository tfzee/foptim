#include "optim/analysis/mem/alias_query.hpp"

#include <algorithm>

#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"

namespace foptim::optim {

AliasResult AliasQuery::alias(const MemLoc &a, const MemLoc &b,
                              bool across_iterations) {
  if (a.ptr.same_base(b.ptr)) {
    if (across_iterations && a.ptr.has_terms()) {
      return AliasResult::May;
    }
    if (a.ptr.offset == b.ptr.offset) {
      return AliasResult::Must;
    }
    if (a.size == 0 || b.size == 0) {
      return AliasResult::May;
    }
    return *a.overlaps(b) ? AliasResult::Partial : AliasResult::No;
  }
  const SetId sa = prov->set_of(a.ptr.addr);
  const SetId sb = prov->set_of(b.ptr.addr);
  const auto &tab = prov->table();
  if (tab.subset(sa, AliasSetTable::Null) ||
      tab.subset(sb, AliasSetTable::Null) || tab.disjoint(sa, sb)) {
    return AliasResult::No;
  }
  return AliasResult::May;
}

namespace {
bool covered_by(const AliasSetTable &tab, SetId u, const IRVec<u32> &sets) {
  auto leaf_covered = [&](SetId leaf) {
    return std::ranges::any_of(sets,
                               [&](SetId s) { return tab.subset(leaf, s); });
  };
  if (!tab.is_union(u)) {
    return leaf_covered(u);
  }
  return std::ranges::all_of(tab.parts(u), leaf_covered);
}

// true if x excludes every scope of one domain that y is in
bool scope_excludes(const AliasSetTable &tab, const fir::AccessMeta &x,
                    const fir::AccessMeta &y) {
  return std::ranges::any_of(
      y.scope_in, [&](SetId u) { return covered_by(tab, u, x.not_in); });
}

struct AccessLoc {
  fir::ValueR ptr;
  u32 size;
};

std::optional<AccessLoc> access_loc(fir::Instr i) {
  if (i->is(fir::InstrType::LoadInstr)) {
    return AccessLoc{i->args[0], i.result_type()->get_size()};
  }
  if (i->is(fir::InstrType::StoreInstr)) {
    return AccessLoc{i->args[0], i->args[1].get_type()->get_size()};
  }
  return std::nullopt;
}
}  // namespace

bool AliasQuery::meta_disjoint(const fir::AccessMeta *a,
                               const fir::AccessMeta *b,
                               bool use_scopes) const {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  const auto &tab = prov->table();
  std::span<const SetId> ta{a->types.data(), a->types.size()};
  std::span<const SetId> tb{b->types.data(), b->types.size()};
  if (tab.disjoint_any(ta, tb)) {
    return true;
  }
  return use_scopes &&
         (scope_excludes(tab, *a, *b) || scope_excludes(tab, *b, *a));
}

AliasResult AliasQuery::alias(const MemLoc &a, const fir::AccessMeta *ma,
                              const MemLoc &b, const fir::AccessMeta *mb,
                              bool across_iterations) {
  const AliasResult r = alias(a, b, across_iterations);
  if (r == AliasResult::No) {
    return r;
  }
  return meta_disjoint(ma, mb, !across_iterations) ? AliasResult::No : r;
}

AliasResult AliasQuery::alias(fir::Instr a, fir::Instr b,
                              bool across_iterations) {
  const auto la = access_loc(a);
  const auto lb = access_loc(b);
  ASSERT(la && lb);
  const auto &fa = a->get_parent()->get_parent();
  const auto &fb = b->get_parent()->get_parent();
  return alias(MemLoc{.ptr = decompose_ptr(la->ptr), .size = la->size},
               fa->get_access_meta(a),
               MemLoc{.ptr = decompose_ptr(lb->ptr), .size = lb->size},
               fb->get_access_meta(b), across_iterations);
}

AliasResult AliasQuery::alias(fir::ValueR a, u32 a_size, fir::ValueR b,
                              u32 b_size, bool across_iterations) {
  return alias(MemLoc{.ptr = decompose_ptr(a), .size = a_size},
               MemLoc{.ptr = decompose_ptr(b), .size = b_size},
               across_iterations);
}

ModRef AliasQuery::access_mod_ref(fir::ValueR ptr, u32 size, const MemLoc &loc,
                                  ModRef kind, const fir::AccessMeta *own,
                                  const fir::AccessMeta *meta) {
  const MemLoc other{.ptr = decompose_ptr(ptr), .size = size};
  return alias(other, own, loc, meta) == AliasResult::No ? ModRef::NoModRef
                                                         : kind;
}

ModRef AliasQuery::call_mod_ref(fir::Instr call, const MemLoc &loc) {
  ModRef r = ModRef::ModRef;
  if (call->args[0].is_constant() && call->args[0].as_constant()->is_func()) {
    const auto &at = call->args[0].as_constant()->as_func()->attribs;
    if (at.mem_read_none) {
      return ModRef::NoModRef;
    }
    if (at.mem_read_only) {
      r = ModRef::Ref;
    } else if (at.mem_write_only) {
      r = ModRef::Mod;
    }
  }
  // a stack object that never escapes can only be reached through the
  // pointers the call is given
  const auto &tab = prov->table();
  const SetId loc_set = prov->set_of(loc.ptr.addr);
  if (!tab.disjoint(loc_set, AliasSetTable::Foreign)) {
    return r;
  }
  for (size_t i = 1; i < call->args.size(); i++) {
    auto arg = call->args[i];
    if (arg.get_type()->is_ptr() && !tab.disjoint(prov->set_of(arg), loc_set)) {
      return r;
    }
  }
  return ModRef::NoModRef;
}

ModRef AliasQuery::mod_ref(fir::Instr instr, const MemLoc &loc,
                           const fir::AccessMeta *meta) {
  const fir::AccessMeta *own =
      instr->has_access_meta
          ? instr->get_parent()->get_parent()->get_access_meta(instr)
          : nullptr;
  if (instr->is(fir::InstrType::LoadInstr)) {
    if (instr->Volatile || instr->Atomic) {
      return ModRef::ModRef;
    }
    return access_mod_ref(instr->args[0], instr.result_type()->get_size(), loc,
                          ModRef::Ref, own, meta);
  }
  if (instr->is(fir::InstrType::StoreInstr)) {
    if (instr->Volatile || instr->Atomic) {
      return ModRef::ModRef;
    }
    return access_mod_ref(instr->args[0], instr->args[1].get_type()->get_size(),
                          loc, ModRef::Mod, own, meta);
  }
  if (instr->is(fir::InstrType::CallInstr)) {
    return call_mod_ref(instr, loc);
  }
  if (instr->is(fir::InstrType::Intrinsic)) {
    auto bytes = [&]() -> u32 {
      if (!instr->args[2].is_constant_int()) {
        return 0;
      }
      i128 unit = 1;
      if (instr->is(fir::IntrinsicSubType::Memset)) {
        unit = instr->args[1].get_type()->get_size();
      } else if (!instr->is(fir::IntrinsicSubType::Memcmp)) {
        unit = instr->memcpy_elem_size();
      }
      const i128 n = instr->args[2].as_constant()->as_int() * unit;
      return n > 0 && n < UINT32_MAX ? static_cast<u32>(n) : 0;
    };
    if (!instr->Volatile) {
      if (instr->is(fir::IntrinsicSubType::Memset)) {
        return access_mod_ref(instr->args[0], bytes(), loc, ModRef::Mod);
      }
      if (instr->is(fir::IntrinsicSubType::Memcpy) ||
          instr->is(fir::IntrinsicSubType::Memmove)) {
        const u32 n = bytes();
        const bool w = access_mod_ref(instr->args[0], n, loc, ModRef::Mod) !=
                       ModRef::NoModRef;
        const bool rd = access_mod_ref(instr->args[1], n, loc, ModRef::Ref) !=
                        ModRef::NoModRef;
        return static_cast<ModRef>((w ? 2 : 0) | (rd ? 1 : 0));
      }
      if (instr->is(fir::IntrinsicSubType::Memcmp)) {
        const u32 n = bytes();
        const bool rd = access_mod_ref(instr->args[0], n, loc, ModRef::Ref) !=
                            ModRef::NoModRef ||
                        access_mod_ref(instr->args[1], n, loc, ModRef::Ref) !=
                            ModRef::NoModRef;
        return rd ? ModRef::Ref : ModRef::NoModRef;
      }
    }
  }
  const bool w = instr->pot_modifies_mem();
  const bool rd = instr->pot_reads_mem();
  return static_cast<ModRef>((w ? 2 : 0) | (rd ? 1 : 0));
}

}  // namespace foptim::optim
