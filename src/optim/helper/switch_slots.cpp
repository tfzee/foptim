#include "optim/helper/switch_slots.hpp"

#include <algorithm>

#include "ir/instruction_data.hpp"

namespace foptim::optim {

std::optional<SwitchCases> SwitchCases::parse(fir::Instr term) {
  if (!term->is(fir::InstrType::SwitchInstr)) {
    return std::nullopt;
  }
  if (term->bbs.size() <= 1 || term->args.size() != term->bbs.size()) {
    return std::nullopt;
  }
  const auto value = term->args.back();
  if (!value.get_type()->is_int() || value.get_type()->get_bitwidth() < 8 ||
      value.get_type()->get_bitwidth() > 64) {
    return std::nullopt;
  }
  SwitchCases res;
  res.term = term;
  res.value_bits = value.get_type()->get_bitwidth();
  const size_t n_cases = term->bbs.size() - 1;
  for (size_t i = 0; i < n_cases; i++) {
    if (!term->args[i].is_constant() ||
        !term->args[i].as_constant()->is_int()) {
      return std::nullopt;
    }
    res.cases.push_back({.value = term->args[i].as_constant()->as_int(),
                         .edge = static_cast<u32>(i)});
  }
  return res;
}

bool SwitchCases::default_unreachable() const {
  const auto bb = term->bbs.back().bb;
  return bb->instructions.size() == 1 &&
         bb->get_terminator()->is(fir::InstrType::Unreachable);
}

fir::BasicBlock SwitchCases::dominant_target(u32 &count) const {
  TMap<fir::BasicBlock, u32> counts;
  fir::BasicBlock target = term->bbs[cases[0].edge].bb;
  count = 0;
  for (const auto &c : cases) {
    const auto bb = term->bbs[c.edge].bb;
    const u32 n = ++counts[bb];
    if (n > count) {
      count = n;
      target = bb;
    }
  }
  return target;
}

TVec<SwitchCases::Case> SwitchCases::of_target(fir::BasicBlock target) const {
  TVec<Case> res;
  for (const auto &c : cases) {
    if (term->bbs[c.edge].bb == target) {
      res.push_back(c);
    }
  }
  std::ranges::sort(res, {}, &Case::value);
  return res;
}

namespace {
u64 value_mask(u32 bits) { return bits >= 64 ? ~u64{0} : (u64{1} << bits) - 1; }

u64 unsigned_value(i128 v, u32 bits) {
  return static_cast<u64>(v) & value_mask(bits);
}

u64 splitmix(u64 &state) {
  state += UINT64_C(0x9E3779B97F4A7C15);
  u64 z = state;
  z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
  z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
  return z ^ (z >> 31);
}

// cuts at the n_runs-1 largest gaps (the leftmost on a tie), the length of a
// run saturates so that the sums cannot overflow
TVec<SlotMap::Run> split_runs(std::span<const i128> values, u32 n_runs) {
  constexpr i128 MaxLen = i128{1} << 28;
  TVec<u32> gaps;
  for (u32 i = 1; i < values.size(); i++) {
    gaps.push_back(i);
  }
  std::ranges::stable_sort(gaps, [&](u32 a, u32 b) {
    return values[a] - values[a - 1] > values[b] - values[b - 1];
  });
  gaps.resize(std::min<size_t>(gaps.size(), n_runs - 1));
  std::ranges::sort(gaps);
  gaps.push_back(static_cast<u32>(values.size()));
  TVec<SlotMap::Run> runs;
  u32 begin = 0;
  u32 base = 0;
  for (auto end : gaps) {
    const auto len =
        static_cast<u32>(std::min(values[end - 1] - values[begin] + 1, MaxLen));
    runs.push_back({.lo = values[begin], .len = len, .base = base});
    base += len;
    begin = end;
  }
  return runs;
}

u64 host_pext(u64 v, u64 m) {
  u64 res = 0;
  for (u64 bit = 1; m != 0; bit <<= 1) {
    const u64 low = m & (~m + 1);
    if ((v & low) != 0) {
      res |= bit;
    }
    m &= m - 1;
  }
  return res;
}

// all values are lo + k * 2^shift
std::optional<SlotMap> plan_stride(std::span<const i128> values, u32 value_bits,
                                   const SlotLimits &limits) {
  i128 g = 0;
  for (auto v : values) {
    g |= v - values[0];
  }
  if (g == 0) {
    return std::nullopt;
  }
  u32 shift = 0;
  while (((g >> shift) & 1) == 0) {
    shift++;
  }
  if (shift == 0 || shift > 62) {
    return std::nullopt;
  }
  const i128 slots = ((values.back() - values.front()) >> shift) + 1;
  if (slots > limits.max_entries ||
      slots > limits.max_slots_per_case * static_cast<i128>(values.size())) {
    return std::nullopt;
  }
  SlotMap map;
  map.kind = SlotMap::Kind::Stride;
  map.value_bits = value_bits;
  map.n_slots = static_cast<u64>(slots);
  map.shift = shift;
  map.runs.push_back({values.front(), static_cast<u32>(slots), 0});
  return map;
}

size_t distinct_masked(std::span<const u64> keys, u64 mask, TVec<u64> &tmp) {
  tmp.clear();
  for (auto k : keys) {
    tmp.push_back(k & mask);
  }
  std::ranges::sort(tmp);
  return static_cast<size_t>(std::ranges::unique(tmp).begin() - tmp.begin());
}

std::optional<SlotMap> plan_mask(std::span<const i128> values, u32 value_bits,
                                 const SlotLimits &limits, bool miss_possible,
                                 bool holes_ok) {
  const size_t n = values.size();
  if (!limits.bmi2 || n < 4) {
    return std::nullopt;
  }
  TVec<u64> keys(n);
  u64 varying = 0;
  for (size_t i = 0; i < n; i++) {
    keys[i] = unsigned_value(values[i], value_bits);
    varying |= keys[i] ^ keys[0];
  }
  const i128 max_slots = std::min<i128>(
      limits.max_entries, limits.max_slots_per_case * static_cast<i128>(n));
  auto make = [&](u64 mask, bool pure) {
    SlotMap map;
    map.kind = SlotMap::Kind::Mask;
    map.value_bits = value_bits;
    map.mask = mask;
    map.pure_mask = pure;
    map.fixed = keys[0] & ~mask & value_mask(value_bits <= 32 ? 32 : 64);
    map.n_slots = u64{1} << std::popcount(mask);
    return map;
  };
  if ((i128{1} << std::popcount(varying)) <= max_slots &&
      (!miss_possible || holes_ok ||
       (i128{1} << std::popcount(varying)) == static_cast<i128>(n))) {
    return make(varying, true);
  }
  // fewer bits than the varying ones, a key column proves membership
  if (n < limits.min_hash_cases || n > limits.max_hash_cases ||
      (value_bits != 8 && value_bits != 16 && value_bits != 32 &&
       value_bits != 64)) {
    return std::nullopt;
  }
  u64 mask = 0;
  TVec<u64> tmp;
  size_t distinct = 1;
  while (distinct < n) {
    if ((i128{1} << (std::popcount(mask) + 1)) > max_slots) {
      return std::nullopt;
    }
    u64 best_bit = 0;
    size_t best = 0;
    for (u64 rest = varying & ~mask; rest != 0; rest &= rest - 1) {
      const u64 bit = rest & (~rest + 1);
      const size_t d = distinct_masked(keys, mask | bit, tmp);
      if (d > best) {
        best = d;
        best_bit = bit;
      }
    }
    if (best_bit == 0 || best <= distinct) {
      return std::nullopt;
    }
    mask |= best_bit;
    distinct = best;
  }
  return make(mask, false);
}

std::optional<SlotMap> plan_mul_shift(std::span<const i128> values,
                                      u32 value_bits,
                                      const SlotLimits &limits) {
  const size_t n = values.size();
  if (n > limits.max_hash_cases || n < limits.min_hash_cases ||
      (value_bits != 8 && value_bits != 16 && value_bits != 32 &&
       value_bits != 64)) {
    return std::nullopt;
  }
  u32 bits = 1;
  while ((size_t{1} << bits) < n) {
    bits++;
  }
  TVec<u64> keys(n);
  for (size_t i = 0; i < n; i++) {
    keys[i] = unsigned_value(values[i], value_bits);
  }
  TVec<u8> used;
  u64 state = 0;
  u32 tries = 0;
  // the table may be up to 4 times the number of cases
  for (u32 b = bits;
       b <= bits + 2 && (i128{1} << b) <= limits.max_entries &&
       (i128{1} << b) <= limits.max_slots_per_case * static_cast<i128>(n);
       b++) {
    const size_t m = size_t{1} << b;
    // a few candidates per size before growing the table
    const u32 per_size = limits.max_hash_tries / 3;
    for (u32 t = 0; t < per_size && tries < limits.max_hash_tries;
         t++, tries++) {
      const u64 mul = splitmix(state) | 1;
      used.assign(m, 0);
      bool ok = true;
      for (auto k : keys) {
        auto &u = used[(k * mul) >> (64 - b)];
        if (u != 0) {
          ok = false;
          break;
        }
        u = 1;
      }
      if (ok) {
        SlotMap map;
        map.kind = SlotMap::Kind::MulShift;
        map.value_bits = value_bits;
        map.n_slots = m;
        map.mul = mul;
        map.shift = 64 - b;
        return map;
      }
    }
  }
  return std::nullopt;
}
}  // namespace

std::optional<SlotMap> plan_slots(std::span<const i128> values, u32 value_bits,
                                  const SlotLimits &limits, bool miss_possible,
                                  bool holes_ok) {
  if (values.empty()) {
    return std::nullopt;
  }
  const i128 n = static_cast<i128>(values.size());
  const auto usable = [&](const SlotMap &map) {
    return !miss_possible || holes_ok || map.needs_keys() ||
           static_cast<i128>(map.n_slots) == n;
  };
  for (u32 r = 1; r <= limits.max_runs && r <= values.size(); r++) {
    if (r == 2) {
      // one table and one check, cheaper than a chain of range checks
      auto stride = plan_stride(values, value_bits, limits);
      if (stride && usable(*stride)) {
        return stride;
      }
    }
    auto runs = split_runs(values, r);
    // runs of one value on average are a compare chain with a load
    if (runs.size() > 1 && values.size() < 2 * runs.size()) {
      continue;
    }
    const i128 total = static_cast<i128>(runs.back().base) + runs.back().len;
    if (total > limits.max_entries || total > limits.max_slots_per_case * n) {
      continue;
    }
    SlotMap map;
    map.kind = runs.size() == 1 ? SlotMap::Kind::Linear : SlotMap::Kind::Runs;
    map.value_bits = value_bits;
    map.n_slots = static_cast<u64>(total);
    map.runs = std::move(runs);
    if (usable(map)) {
      return map;
    }
  }
  if (auto mask =
          plan_mask(values, value_bits, limits, miss_possible, holes_ok)) {
    return mask;
  }
  return plan_mul_shift(values, value_bits, limits);
}

fir::Global make_slot_global(fir::Function &func, size_t bytes, u32 align) {
  IRString name;
  fmt::format_to(std::back_inserter(name), "lookup_table_{}_{}",
                 func.name.c_str(), func.n_lookup_tables++);
  auto global = func.ctx->insert_global(std::move(name), bytes);
  global->is_constant = true;
  global->linkage = fir::Linkage::Internal;
  global->min_align = align;
  global->init_value = foptim::utils::IRAlloc<uint8_t>{}.allocate(bytes);
  memset(global->init_value, 0, bytes);
  return global;
}

u64 SlotMap::slot(i128 v) const {
  if (kind == Kind::MulShift) {
    return (unsigned_value(v, value_bits) * mul) >> shift;
  }
  if (kind == Kind::Mask) {
    return host_pext(unsigned_value(v, value_bits), mask);
  }
  if (kind == Kind::Stride) {
    return static_cast<u64>(v - runs[0].lo) >> shift;
  }
  for (const auto &run : runs) {
    if (v >= run.lo && v < run.lo + run.len) {
      return run.base + static_cast<u64>(v - run.lo);
    }
  }
  ASSERT_M(false, "value is not in any run");
  return 0;
}

void SlotMap::fill_keys(std::span<const i128> values, u8 *out) const {
  ASSERT(needs_keys());
  const u32 size = key_size();
  const u64 filler = unsigned_value(values[0], value_bits);
  for (u64 s = 0; s < n_slots; s++) {
    memcpy(out + (s * size), &filler, size);
  }
  for (auto v : values) {
    const u64 k = unsigned_value(v, value_bits);
    memcpy(out + (slot(v) * size), &k, size);
  }
}

SlotMap::Dispatch SlotMap::emit(fir::ContextData *ctx, fir::Builder &b,
                                fir::ValueR value, fir::BasicBlock from,
                                const fir::BBRefWithArgs &miss,
                                bool miss_possible, fir::Global keys) const {
  auto i64_ty = ctx->get_int_type(64);
  auto c64 = [&](i128 v) { return fir::ValueR{ctx->get_constant_int(v, 64)}; };
  auto add_miss_args = [&](fir::Instr br, u16 idx) {
    for (auto arg : miss.args) {
      br.add_bb_arg(idx, arg);
    }
  };

  b.at_end(from);
  auto hit = b.append_bb();
  b.at_end(from);

  if (kind == Kind::MulShift || kind == Kind::Mask) {
    fir::ValueR s;
    fir::ValueR u = value;
    if (kind == Kind::MulShift) {
      if (value_bits < 64) {
        u = b.build_zext(u, i64_ty);
      }
      s = b.build_lshr(b.build_int_mul(u, c64(static_cast<i64>(mul))),
                       c64(shift));
    } else {
      const u32 width = value_bits <= 32 ? 32 : 64;
      auto wide_ty = ctx->get_int_type(width);
      if (value_bits < width) {
        u = b.build_zext(u, wide_ty);
      }
      s = b.build_intrinsic(
          u, fir::ValueR{ctx->get_constant_int(static_cast<i64>(mask), width)},
          fir::IntrinsicSubType::Pext, wide_ty);
      if (width < 64) {
        s = b.build_zext(s, i64_ty);
      }
    }
    if (!miss_possible) {
      b.build_branch(hit);
    } else {
      fir::ValueR cond;
      if (kind == Kind::Mask && pure_mask) {
        const u32 width = value_bits <= 32 ? 32 : 64;
        auto outside = b.build_binary_op(
            u,
            fir::ValueR{ctx->get_constant_int(
                static_cast<i64>(~mask & value_mask(width)), width)},
            fir::BinaryInstrSubType::And);
        cond = b.build_int_cmp(
            outside,
            fir::ValueR{ctx->get_constant_int(static_cast<i64>(fixed), width)},
            fir::ICmpInstrSubType::EQ);
      } else {
        fir::ValueR off = s;
        if (key_size() > 1) {
          off = b.build_int_mul(s, c64(key_size()));
        }
        auto addr =
            b.build_ptr_add(fir::ValueR{ctx->get_constant_value(keys)}, off);
        auto key = b.build_load(value.get_type(), addr, false, false);
        cond = b.build_int_cmp(key, value, fir::ICmpInstrSubType::EQ);
      }
      auto br = b.build_cond_branch(cond, hit, miss.bb);
      add_miss_args(br, 1);
    }
    return {.hit = hit, .slot = s};
  }

  fir::ValueR x = value;
  if (value_bits < 64) {
    x = b.build_sext(x, i64_ty);
  }
  if (kind == Kind::Stride) {
    fir::ValueR t = x;
    if (runs[0].lo != 0) {
      t = b.build_int_sub(t, c64(runs[0].lo));
    }
    auto s = b.build_lshr(t, c64(shift));
    if (!miss_possible) {
      b.build_branch(hit);
    } else {
      // in range and no bit below the stride set
      auto in_range =
          b.build_int_cmp(t, c64(((i128{n_slots} - 1) << shift) + 1),
                          fir::ICmpInstrSubType::ULT);
      auto low =
          b.build_int_cmp(b.build_binary_op(t, c64((i128{1} << shift) - 1),
                                            fir::BinaryInstrSubType::And),
                          c64(0), fir::ICmpInstrSubType::EQ);
      auto cond =
          b.build_binary_op(in_range, low, fir::BinaryInstrSubType::And);
      auto br = b.build_cond_branch(cond, hit, miss.bb);
      add_miss_args(br, 1);
    }
    return {.hit = hit, .slot = s};
  }
  // with several runs the slot is an argument of hit
  const bool multi = runs.size() > 1;
  fir::ValueR hit_slot;
  if (multi) {
    hit_slot =
        fir::ValueR{hit.add_arg(ctx->storage.insert_bb_arg(hit, i64_ty))};
  }
  fir::ValueR single_slot;
  for (size_t i = 0; i < runs.size(); i++) {
    const auto &run = runs[i];
    const bool last = i + 1 == runs.size();
    fir::ValueR t = x;
    if (run.lo != 0) {
      t = b.build_int_sub(t, c64(run.lo));
    }
    fir::ValueR s = t;
    if (run.base != 0) {
      s = b.build_int_add(t, c64(run.base));
    }
    single_slot = s;
    auto pass_slot = [&](fir::Instr br) {
      if (multi) {
        br.add_bb_arg(0, s);
      }
    };
    if (last && !miss_possible) {
      pass_slot(b.build_branch(hit));
      break;
    }
    auto cond = b.build_int_cmp(t, c64(run.len), fir::ICmpInstrSubType::ULT);
    if (last) {
      auto br = b.build_cond_branch(cond, hit, miss.bb);
      pass_slot(br);
      add_miss_args(br, 1);
    } else {
      auto next = b.append_bb();
      pass_slot(b.build_cond_branch(cond, hit, next));
      b.at_end(next);
    }
  }
  return {.hit = hit, .slot = multi ? hit_slot : single_slot};
}

}  // namespace foptim::optim
