#include "optim/helper/bit_windows.hpp"

#include <algorithm>

#include "optim/cost_model/seq_cost.hpp"
#include "utils/todo.hpp"

namespace foptim::optim {
namespace {
constexpr u64 WindowSpan = 64;

std::optional<BitWindows> cut_windows(std::span<const u64> values, u32 n) {
  TVec<u32> gaps;
  for (u32 i = 1; i < values.size(); i++) {
    gaps.push_back(i);
  }
  std::ranges::stable_sort(gaps, [&](u32 a, u32 b) {
    return values[a] - values[a - 1] > values[b] - values[b - 1];
  });
  gaps.resize(std::min<size_t>(gaps.size(), n - 1));
  std::ranges::sort(gaps);
  gaps.push_back(static_cast<u32>(values.size()));
  BitWindows res;
  u32 begin = 0;
  for (const u32 end : gaps) {
    const u64 lo = values[begin];
    const u64 span = values[end - 1] - lo + 1;
    if (span > WindowSpan) {
      return std::nullopt;
    }
    BitWindow w{.base = lo, .span = static_cast<u32>(span)};
    for (u32 i = begin; i < end; i++) {
      w.mask |= u64{1} << (values[i] - lo);
    }
    res.windows.push_back(w);
    begin = end;
  }
  return res;
}
}  // namespace

std::optional<BitWindows> plan_bit_windows(std::span<const u64> values,
                                           u32 max_windows) {
  if (values.empty()) {
    return std::nullopt;
  }
  for (u32 n = 1; n <= max_windows; n++) {
    auto res = cut_windows(values, n);
    if (!res) {
      continue;
    }
    // the shift masks its count, so everything below 64 needs no subtraction
    if (res->windows.size() == 1 && values.back() < WindowSpan) {
      res->windows[0] = {.base = 0,
                         .span = static_cast<u32>(values.back() + 1),
                         .mask = res->windows[0].mask << values.front()};
    }
    return res;
  }
  return std::nullopt;
}

fir::ValueR emit_bit_windows(fir::ContextData *ctx, fir::Builder &b,
                             fir::ValueR x, const BitWindows &w, bool invert) {
  const u32 bits = x.get_type()->get_bitwidth();
  const auto i64 = ctx->get_int_type(64);
  const auto i1 = ctx->get_int_type(1);
  fir::ValueR res{};
  for (const auto &win : w.windows) {
    fir::ValueR hit;
    if (win.kind() == BitWindow::Kind::Eq) {
      hit = b.build_int_cmp(
          x, fir::ValueR{ctx->get_constant_int(win.member(), bits)},
          fir::ICmpInstrSubType::EQ);
    } else {
      auto idx = x;
      if (win.base != 0) {
        idx = b.build_int_sub(
            idx, fir::ValueR{ctx->get_constant_int(win.base, bits)});
      }
      hit = b.build_int_cmp(idx,
                            fir::ValueR{ctx->get_constant_int(win.span, bits)},
                            fir::ICmpInstrSubType::ULT);
      if (win.kind() == BitWindow::Kind::Mask) {
        if (bits < 64) {
          idx = b.build_zext(idx, i64);
        }
        auto sh =
            b.build_binary_op(idx, fir::ValueR{ctx->get_constant_int(63, 64)},
                              fir::BinaryInstrSubType::And);
        auto word =
            b.build_lshr(fir::ValueR{ctx->get_constant_int(win.mask, 64)}, sh);
        hit = b.build_binary_op(hit, b.build_itrunc(word, i1),
                                fir::BinaryInstrSubType::And);
      }
    }
    res = res.kind == fir::ValueType::InvalidValue
              ? hit
              : b.build_binary_op(res, hit, fir::BinaryInstrSubType::Or);
  }
  if (invert) {
    res = b.build_binary_op(res, fir::ValueR{ctx->get_constant_int(1, 1)},
                            fir::BinaryInstrSubType::Xor);
  }
  return res;
}

namespace {
using cost_model::OpSeq;
using cost_model::X86OpClass;

double score_of(const OpSeq &seq, bool hot, const cost_model::Machine &m) {
  const auto c = cost_model::cost_seq(seq, m);
  constexpr double Minor = 0.25;
  return hot ? c.throughput + (Minor * c.latency)
             : c.latency + (Minor * c.throughput);
}

OpSeq window_seq(const BitWindows &w) {
  OpSeq windows;
  bool have = false;
  u32 acc = 0;
  for (const auto &win : w.windows) {
    u32 hit = 0;
    if (win.kind() == BitWindow::Kind::Eq) {
      hit = windows.add(X86OpClass::SETCC, {windows.add(X86OpClass::ALU_RI)});
    } else {
      u32 idx = windows.add(X86OpClass::MOV_RR);
      if (win.base != 0) {
        idx = windows.add(X86OpClass::ALU_RI, {idx});
      }
      const u32 cmp = windows.add(X86OpClass::ALU_RI, {idx});
      hit = windows.add(X86OpClass::SETCC, {cmp});
      if (win.kind() == BitWindow::Kind::Mask) {
        const u32 count = windows.add(X86OpClass::MOV_RR, {idx});
        const u32 mask = windows.add_const();
        const u32 shr = windows.add(X86OpClass::SHIFT_RR, {mask, count});
        const u32 bit = windows.add(X86OpClass::ALU_RI, {shr});
        hit = windows.add(X86OpClass::ALU_RR, {hit, bit});
      }
    }
    acc = have ? windows.add(X86OpClass::ALU_RR, {acc, hit}) : hit;
    have = true;
  }
  return windows;
}
}  // namespace

LookupChoice choose_windows_or_table(const BitWindows &w, bool hot,
                                     const cost_model::Machine &machine) {
  // the xor of an inverted set is as cheap as the and of the table form, so it
  // is left out of both
  OpSeq table;
  const u32 zext = table.add(X86OpClass::MOV_RR);
  const double load_latency = machine.cache(hot ? 0 : 1).latency;
  const u32 load =
      table.add_with_latency(X86OpClass::LOAD, load_latency, {zext});
  table.add(X86OpClass::ALU_RI, {load});

  LookupChoice res;
  res.window_score = score_of(window_seq(w), hot, machine);
  res.table_score = score_of(table, hot, machine);
  res.windows = res.window_score <= res.table_score;
  return res;
}

LookupChoice choose_windows_or_compares(const BitWindows &w, u32 n_cases,
                                        bool hot,
                                        const cost_model::Machine &machine) {
  OpSeq cmps;
  u32 acc = 0;
  for (u32 i = 0; i < n_cases; i++) {
    const u32 cmp = cmps.add(X86OpClass::ALU_RI);
    const u32 set = cmps.add(X86OpClass::SETCC, {cmp});
    acc = i == 0 ? set : cmps.add(X86OpClass::ALU_RR, {acc, set});
  }
  LookupChoice res;
  res.window_score = score_of(window_seq(w), hot, machine);
  res.table_score = score_of(cmps, hot, machine);
  res.windows = res.window_score <= res.table_score;
  return res;
}
}  // namespace foptim::optim
