#include "optim/analysis/mem/mem_access.hpp"

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"

namespace foptim::optim {

namespace {
constexpr u32 MaxLinearDepth = 6;

// offset + sum(index * scale) of an index operand
struct Linear {
  i64 offset = 0;
  std::array<IndexTerm, PtrShape::MaxTerms> terms{};
  u8 n = 0;
};

bool add_i64(i64 a, i64 b, i64 &out) {
  return !__builtin_add_overflow(a, b, &out);
}
bool mul_i64(i64 a, i64 b, i64 &out) {
  return !__builtin_mul_overflow(a, b, &out);
}

template <class T>
bool add_term(T &dst, u8 &n, fir::ValueR index, i64 scale) {
  for (u8 i = 0; i < n; i++) {
    if (dst[i].index == index) {
      i64 s = 0;
      if (!add_i64(dst[i].scale, scale, s)) {
        return false;
      }
      if (s == 0) {
        dst[i] = dst[n - 1];
        n--;
      } else {
        dst[i].scale = s;
      }
      return true;
    }
  }
  if (scale == 0) {
    return true;
  }
  if (n == PtrShape::MaxTerms) {
    return false;
  }
  dst[n++] = IndexTerm{.index = index, .scale = scale};
  return true;
}

bool const_i64(fir::ValueR v, i64 &out) {
  if (!v.is_constant() || !v.as_constant()->is_int()) {
    return false;
  }
  const auto c = v.as_constant()->as_int();
  if (c < INT64_MIN || c > INT64_MAX) {
    return false;
  }
  out = static_cast<i64>(c);
  return true;
}

bool is_i64(fir::ValueR v) {
  auto ty = v.get_type();
  return ty->is_int() && ty->get_bitwidth() == 64;
}

// mult * v added to lin, false if something overflowed or there are too many
// terms
bool linearize(fir::ValueR v, i64 mult, Linear &lin, u32 depth) {
  i64 c = 0;
  if (const_i64(v, c)) {
    i64 m = 0;
    return mul_i64(c, mult, m) && add_i64(lin.offset, m, lin.offset);
  }
  if (depth < MaxLinearDepth && v.is_instr() && is_i64(v)) {
    auto i = v.as_instr();
    i64 k = 0;
    if (i->is(fir::BinaryInstrSubType::IntAdd)) {
      return linearize(i->args[0], mult, lin, depth + 1) &&
             linearize(i->args[1], mult, lin, depth + 1);
    }
    if (i->is(fir::BinaryInstrSubType::IntSub) && mult != INT64_MIN) {
      return linearize(i->args[0], mult, lin, depth + 1) &&
             linearize(i->args[1], -mult, lin, depth + 1);
    }
    if (i->is(fir::BinaryInstrSubType::IntMul)) {
      for (u32 side = 0; side < 2; side++) {
        i64 m = 0;
        if (const_i64(i->args[side], k) && mul_i64(k, mult, m)) {
          return linearize(i->args[1 - side], m, lin, depth + 1);
        }
      }
    }
    if (i->is(fir::BinaryInstrSubType::Shl) && const_i64(i->args[1], k) &&
        k >= 0 && k < 62) {
      i64 m = 0;
      if (mul_i64(i64{1} << k, mult, m)) {
        return linearize(i->args[0], m, lin, depth + 1);
      }
    }
  }
  return add_term(lin.terms, lin.n, v, mult);
}
}  // namespace

bool PtrShape::same_base(const PtrShape &o) const {
  return base == o.base && same_terms(o);
}

bool PtrShape::same_terms(const PtrShape &o) const {
  if (n_terms != o.n_terms) {
    return false;
  }
  for (u8 i = 0; i < n_terms; i++) {
    bool found = false;
    for (u8 j = 0; j < o.n_terms && !found; j++) {
      found = terms_[i] == o.terms_[j];
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

PtrShape decompose_ptr(fir::ValueR addr) {
  PtrShape s;
  s.addr = addr;
  s.base = addr;
  s.anchor = addr;
  bool anchor_open = true;
  while (s.base.is_instr()) {
    auto i = s.base.as_instr();
    if (!i->is(fir::BinaryInstrSubType::PtrAdd)) {
      break;
    }
    Linear lin;
    if (!linearize(i->args[1], 1, lin, 0)) {
      lin = Linear{};
      if (!add_term(lin.terms, lin.n, i->args[1], 1)) {
        break;
      }
    }
    i64 offset = 0;
    if (!add_i64(s.offset, lin.offset, offset)) {
      break;
    }
    auto terms = s.terms_;
    u8 n = s.n_terms;
    bool ok = true;
    for (u8 t = 0; t < lin.n && ok; t++) {
      ok = add_term(terms, n, lin.terms[t].index, lin.terms[t].scale);
    }
    if (!ok) {
      break;
    }
    if (anchor_open) {
      if (lin.n == 0) {
        i64 ao = 0;
        if (add_i64(s.anchor_offset, lin.offset, ao)) {
          s.anchor_offset = ao;
          s.anchor = i->args[0];
        } else {
          anchor_open = false;
        }
      } else {
        anchor_open = false;
      }
    }
    s.offset = offset;
    s.terms_ = terms;
    s.n_terms = n;
    s.base = i->args[0];
  }
  return s;
}

std::optional<bool> MemLoc::overlaps(const MemLoc &o) const {
  if (!ptr.same_base(o.ptr) || size == 0 || o.size == 0) {
    return std::nullopt;
  }
  const i128 a = ptr.offset;
  const i128 b = o.ptr.offset;
  return a < b + o.size && b < a + size;
}

std::optional<MemAccess> classify_access(fir::Instr instr) {
  using Kind = MemAccess::Kind;
  MemAccess a;
  a.instr = instr;
  const bool is_load = instr->is(fir::InstrType::LoadInstr);
  const bool is_store = instr->is(fir::InstrType::StoreInstr);
  if (is_load || is_store) {
    a.ordered = instr->Atomic || instr->Volatile;
    a.kind = a.ordered ? Kind::BarrierRW : (is_load ? Kind::Load : Kind::Store);
    a.loc.ptr = decompose_ptr(instr->args[0]);
    a.loc.size = instr.result_type()->get_size();
    return a;
  }
  if (instr->pot_modifies_mem()) {
    a.kind = Kind::BarrierRW;
    return a;
  }
  if (instr->pot_reads_mem()) {
    a.kind = Kind::BarrierR;
    return a;
  }
  return std::nullopt;
}

MemAccessInfo::MemAccessInfo(fir::Function &func) {
  for (const auto bb : func.basic_blocks) {
    for (const auto instr : bb->instructions) {
      auto a = classify_access(instr);
      if (!a) {
        continue;
      }
      if (instr->is(fir::InstrType::LoadInstr) ||
          instr->is(fir::InstrType::StoreInstr)) {
        index.insert({instr.result(), static_cast<u32>(acc.size())});
      }
      acc.push_back(*a);
    }
  }
}

const MemAccess *MemAccessInfo::find(fir::Instr instr) const {
  auto it = index.find(instr.result());
  return it == index.end() ? nullptr : &acc[it->second];
}

bool MemAccessInfo::equivalent(const MemAccessInfo &o) const {
  if (acc.size() != o.acc.size()) {
    return false;
  }
  for (size_t i = 0; i < acc.size(); i++) {
    const auto &a = acc[i];
    const auto &b = o.acc[i];
    if (a.instr != b.instr || a.kind != b.kind || a.ordered != b.ordered ||
        a.loc.size != b.loc.size) {
      return false;
    }
    if (a.instr->is(fir::InstrType::LoadInstr) ||
        a.instr->is(fir::InstrType::StoreInstr)) {
      if (!(a.loc.ptr == b.loc.ptr) || a.loc.ptr.anchor != b.loc.ptr.anchor ||
          a.loc.ptr.anchor_offset != b.loc.ptr.anchor_offset) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace foptim::optim
