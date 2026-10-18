#include "optim/module_passes/legalize_ints.hpp"

#include <optional>

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "utils/APInt.hpp"
#include "utils/map.hpp"

namespace foptim::optim {

namespace {

using Limbs = TVec<fir::ValueR>;
using BinOp = fir::BinaryInstrSubType;
using Cmp = fir::ICmpInstrSubType;

// Number of i64 limbs a type is split into, 0 when it is legal. The one place
// that decides which types are split.
u32 n_limbs(fir::TypeR ty) {
  if (!ty->is_int()) {
    return 0;
  }
  const u32 width = ty->get_bitwidth();
  return width > 64 ? (width + 63) / 64 : 0;
}

bool value_is_wide(fir::ValueR v) { return n_limbs(v.get_type()) != 0; }

bool func_type_is_wide(const fir::FunctionType &ft) {
  for (auto t : ft.return_types) {
    if (n_limbs(t) != 0) {
      return true;
    }
  }
  for (auto t : ft.arg_types) {
    if (n_limbs(t) != 0) {
      return true;
    }
  }
  return false;
}

fir::FunctionTypeR legal_func_type(fir::Context &ctx,
                                   const fir::FunctionType &ft) {
  const fir::TypeR i64 = ctx->get_int_type(64);
  IRVec<fir::TypeR> rets;
  u32 n_int = 0;
  for (auto t : ft.return_types) {
    const u32 n = n_limbs(t);
    if (n == 0) {
      rets.push_back(t);
      n_int += t->is_int() || t->is_ptr() ? 1 : 0;
      continue;
    }
    for (u32 i = 0; i < n; i++) {
      rets.push_back(i64);
    }
    n_int += n;
  }
  if (n_int > 2) {
    IMPL("wide integer result that does not fit the return registers");
  }
  IRVec<fir::TypeR> args;
  for (auto t : ft.arg_types) {
    const u32 n = n_limbs(t);
    if (n == 0) {
      args.push_back(t);
    }
    for (u32 i = 0; i < n; i++) {
      args.push_back(i64);
    }
  }
  return ctx->get_func_ty(std::span<const fir::TypeR>{rets}, std::move(args));
}

class Expander {
  fir::Context &ctx;
  fir::Function &func;
  fir::TypeR i64;
  TMap<fir::ValueR, Limbs> map;
  TVec<fir::Instr> dead;
  bool changed = false;

  struct WideArg {
    u32 index;
    Limbs limbs;
  };
  TMap<fir::BasicBlock, TVec<WideArg>> wide_args;

  fir::ValueR c64(u64 v) { return fir::ValueR{ctx->get_constant_int(v, 64)}; }

  fir::ValueR poison64() { return fir::ValueR{ctx->get_poisson_value(i64)}; }

  Limbs get(fir::ValueR v) {
    const u32 n = n_limbs(v.get_type());
    ASSERT(n != 0);
    if (v.is_constant()) {
      auto c = v.as_constant();
      Limbs r;
      if (c->is_poison()) {
        for (u32 i = 0; i < n; i++) {
          r.push_back(poison64());
        }
        return r;
      }
      ASSERT(c->is_int());
      const APInt &a = c->as_apint();
      for (u32 i = 0; i < n; i++) {
        r.push_back(c64(i < a.num_words() ? a.data()[i] : 0));
      }
      return r;
    }
    auto it = map.find(v);
    ASSERT_M(it != map.end(), "wide value without limbs");
    return it->second;
  }

  fir::ValueR bin(fir::Builder &b, BinOp op, fir::ValueR x, fir::ValueR y) {
    return b.build_binary_op(x, y, op);
  }
  fir::ValueR cmp(fir::Builder &b, Cmp p, fir::ValueR x, fir::ValueR y) {
    return b.build_int_cmp(x, y, p);
  }
  fir::ValueR carry_of(fir::Builder &b, fir::ValueR c) {
    return b.build_zext(c, i64);
  }

  // Bits above the width in the top limb are unspecified, operations that read
  // them extend first.
  Limbs zext_norm(fir::Builder &b, Limbs x, u32 width) {
    const u32 rem = width % 64;
    if (rem != 0) {
      x.back() = bin(b, BinOp::And, x.back(), c64((u64{1} << rem) - 1));
    }
    return x;
  }
  Limbs sext_norm(fir::Builder &b, Limbs x, u32 width) {
    const u32 rem = width % 64;
    if (rem != 0) {
      x.back() =
          b.build_ashr(b.build_shl(x.back(), c64(64 - rem)), c64(64 - rem));
    }
    return x;
  }
  Limbs norm(fir::Builder &b, Limbs x, u32 width, bool is_signed) {
    return is_signed ? sext_norm(b, std::move(x), width)
                     : zext_norm(b, std::move(x), width);
  }

  fir::ValueR widen_narrow(fir::Builder &b, fir::ValueR v, bool is_signed) {
    const u32 w = v.get_type()->get_bitwidth();
    if (w == 64) {
      return v;
    }
    return is_signed ? b.build_sext(v, i64) : b.build_zext(v, i64);
  }

  Limbs add_limbs(fir::Builder &b, const Limbs &x, const Limbs &y) {
    Limbs r;
    std::optional<fir::ValueR> carry;
    for (size_t i = 0; i < x.size(); i++) {
      auto s = bin(b, BinOp::IntAdd, x[i], y[i]);
      auto carry_out = cmp(b, Cmp::ULT, s, x[i]);
      if (carry) {
        auto t = bin(b, BinOp::IntAdd, s, *carry);
        carry_out = bin(b, BinOp::Or, carry_out, cmp(b, Cmp::ULT, t, s));
        s = t;
      }
      r.push_back(s);
      if (i + 1 < x.size()) {
        carry = carry_of(b, carry_out);
      }
    }
    return r;
  }

  Limbs sub_limbs(fir::Builder &b, const Limbs &x, const Limbs &y) {
    Limbs r;
    std::optional<fir::ValueR> borrow;
    for (size_t i = 0; i < x.size(); i++) {
      auto d = bin(b, BinOp::IntSub, x[i], y[i]);
      auto borrow_out = cmp(b, Cmp::ULT, x[i], y[i]);
      if (borrow) {
        auto t = bin(b, BinOp::IntSub, d, *borrow);
        borrow_out =
            bin(b, BinOp::Or, borrow_out, cmp(b, Cmp::ULT, d, *borrow));
        d = t;
      }
      r.push_back(d);
      if (i + 1 < x.size()) {
        borrow = carry_of(b, borrow_out);
      }
    }
    return r;
  }

  fir::ValueR umulh(fir::Builder &b, fir::ValueR x, fir::ValueR y) {
    const auto mask = c64(0xffffffff);
    auto xl = bin(b, BinOp::And, x, mask);
    auto xh = b.build_lshr(x, c64(32));
    auto yl = bin(b, BinOp::And, y, mask);
    auto yh = b.build_lshr(y, c64(32));
    auto p0 = bin(b, BinOp::IntMul, xl, yl);
    auto p1 = bin(b, BinOp::IntMul, xl, yh);
    auto p2 = bin(b, BinOp::IntMul, xh, yl);
    auto p3 = bin(b, BinOp::IntMul, xh, yh);
    auto mid = bin(b, BinOp::IntAdd,
                   bin(b, BinOp::IntAdd, b.build_lshr(p0, c64(32)),
                       bin(b, BinOp::And, p1, mask)),
                   bin(b, BinOp::And, p2, mask));
    auto hi = bin(b, BinOp::IntAdd, p3, b.build_lshr(p1, c64(32)));
    hi = bin(b, BinOp::IntAdd, hi, b.build_lshr(p2, c64(32)));
    return bin(b, BinOp::IntAdd, hi, b.build_lshr(mid, c64(32)));
  }

  // r += v << (64 * pos), the carry ripples up to the top limb
  void add_at(fir::Builder &b, Limbs &r, size_t pos, fir::ValueR v) {
    for (; pos < r.size(); pos++) {
      if (r[pos].is_const_int(0)) {
        r[pos] = v;
        return;
      }
      auto s = bin(b, BinOp::IntAdd, r[pos], v);
      auto c = cmp(b, Cmp::ULT, s, v);
      r[pos] = s;
      if (pos + 1 == r.size()) {
        return;
      }
      v = carry_of(b, c);
    }
  }

  Limbs mul_limbs(fir::Builder &b, const Limbs &x, const Limbs &y) {
    const size_t n = x.size();
    Limbs r(n, c64(0));
    for (size_t i = 0; i < n; i++) {
      for (size_t k = 0; i + k < n; k++) {
        add_at(b, r, i + k, bin(b, BinOp::IntMul, x[i], y[k]));
        if (i + k + 1 < n) {
          add_at(b, r, i + k + 1, umulh(b, x[i], y[k]));
        }
      }
    }
    return r;
  }

  // x shifted by 64 * qi + bit bits, bit is in [0, 63]
  Limbs shift_candidate(fir::Builder &b, BinOp kind, const Limbs &x, size_t qi,
                        fir::ValueR bit, std::optional<u64> bit_const,
                        fir::ValueR fill) {
    const size_t n = x.size();
    const auto inv =
        bit_const ? c64(63 ^ *bit_const) : bin(b, BinOp::Xor, bit, c64(63));
    const bool bit_zero = bit_const && *bit_const == 0;
    Limbs r;
    for (size_t j = 0; j < n; j++) {
      if (kind == BinOp::Shl) {
        if (j < qi) {
          r.push_back(c64(0));
          continue;
        }
        const size_t idx = j - qi;
        auto out = b.build_shl(x[idx], bit);
        if (idx > 0 && !bit_zero) {
          out = bin(b, BinOp::Or, out,
                    b.build_lshr(b.build_lshr(x[idx - 1], c64(1)), inv));
        }
        r.push_back(out);
        continue;
      }
      const size_t idx = j + qi;
      if (idx >= n) {
        r.push_back(fill);
        continue;
      }
      auto out = kind == BinOp::AShr && idx == n - 1
                     ? b.build_ashr(x[idx], bit)
                     : b.build_lshr(x[idx], bit);
      if (!bit_zero) {
        if (idx + 1 < n) {
          out = bin(b, BinOp::Or, out,
                    b.build_shl(b.build_shl(x[idx + 1], c64(1)), inv));
        } else if (kind == BinOp::AShr) {
          out = bin(b, BinOp::Or, out,
                    b.build_shl(b.build_shl(fill, c64(1)), inv));
        }
      }
      r.push_back(out);
    }
    return r;
  }

  Limbs shift_limbs(fir::Builder &b, BinOp kind, Limbs x, fir::ValueR amount,
                    u32 width) {
    const size_t n = x.size();
    if (kind == BinOp::Shr) {
      x = zext_norm(b, std::move(x), width);
    } else if (kind == BinOp::AShr) {
      x = sext_norm(b, std::move(x), width);
    }
    fir::ValueR fill = c64(0);
    if (kind == BinOp::AShr) {
      fill = b.build_ashr(x.back(), c64(63));
    }
    if (amount.is_constant_int()) {
      const auto amt = amount.as_constant()->as_int_sat();
      if (amt < 0 || amt >= width) {
        return Limbs(n, poison64());
      }
      return shift_candidate(b, kind, x, static_cast<size_t>(amt / 64),
                             c64(amt % 64), static_cast<u64>(amt % 64), fill);
    }
    auto q = b.build_lshr(amount, c64(6));
    auto bit = bin(b, BinOp::And, amount, c64(63));
    Limbs res = shift_candidate(b, kind, x, n - 1, bit, std::nullopt, fill);
    for (size_t qi = n - 1; qi-- > 0;) {
      auto cand = shift_candidate(b, kind, x, qi, bit, std::nullopt, fill);
      auto is_q = cmp(b, Cmp::EQ, q, c64(qi));
      for (size_t j = 0; j < n; j++) {
        res[j] = b.build_select(i64, is_q, cand[j], res[j]);
      }
    }
    return res;
  }

  fir::ValueR amount_i64(fir::Builder &b, fir::ValueR amount) {
    if (value_is_wide(amount)) {
      if (amount.is_constant()) {
        auto c = amount.as_constant();
        if (!c->is_poison()) {
          return c64(c->as_apint().data()[0]);
        }
      }
      return get(amount)[0];
    }
    return widen_narrow(b, amount, false);
  }

  fir::ValueR libcall(const char *name, fir::FunctionTypeR &type_out) {
    const fir::TypeR i64 = ctx->get_int_type(64);
    IRVec<fir::TypeR> rets = {i64, i64};
    IRVec<fir::TypeR> args = {i64, i64, i64, i64};
    type_out =
        ctx->get_func_ty(std::span<const fir::TypeR>{rets}, std::move(args));
    if (!ctx->has_function(name)) {
      IRString s = name;
      ctx.data->storage.functions.insert(
          s, std::make_unique<fir::Function>(ctx.operator->(), s, type_out));
    }
    return fir::ValueR{ctx->get_constant_value(ctx->get_function(name))};
  }

  Limbs divrem(fir::Builder &b, BinOp op, const Limbs &x, const Limbs &y,
               u32 width) {
    if (x.size() != 2) {
      IMPL("division of wide integers above 128 bits");
    }
    const bool is_signed = op == BinOp::IntSDiv || op == BinOp::IntSRem;
    const bool is_div = op == BinOp::IntSDiv || op == BinOp::IntUDiv;
    const char *name = is_signed ? (is_div ? "__divti3" : "__modti3")
                                 : (is_div ? "__udivti3" : "__umodti3");
    auto xn = norm(b, x, width, is_signed);
    auto yn = norm(b, y, width, is_signed);
    fir::FunctionTypeR ft{};
    auto callee = libcall(name, ft);
    fir::ValueR args[4] = {xn[0], xn[1], yn[0], yn[1]};
    const fir::TypeR rets[2] = {i64, i64};
    auto call = b.build_call(callee, ft, std::span<const fir::TypeR>{rets},
                             std::span<fir::ValueR>{args});
    return {call.result(0), call.result(1)};
  }

  // splits the 1 to 7 bytes of a partial top limb into 4, 2 and 1 byte pieces
  template <class F>
  void for_pieces(u32 bytes, F &&f) {
    u32 off = 0;
    for (u32 sz : {4u, 2u, 1u}) {
      if ((bytes & sz) != 0) {
        f(off, sz);
        off += sz;
      }
    }
  }

  fir::ValueR addr(fir::Builder &b, fir::ValueR ptr, u64 off) {
    return off == 0 ? ptr : b.build_ptr_add(ptr, c64(off));
  }

  Limbs load_limbs(fir::Builder &b, fir::Instr I, u32 width) {
    const u32 n = (width + 63) / 64;
    const u32 bytes = (width + 7) / 8;
    auto ptr = I->args[0];
    Limbs r;
    for (u32 i = 0; i < n; i++) {
      const u32 size = std::min(8u, bytes - 8 * i);
      const auto a = addr(b, ptr, 8ull * i);
      if (size == 8) {
        r.push_back(b.build_load(i64, a, false, I->Volatile));
        continue;
      }
      std::optional<fir::ValueR> acc;
      for_pieces(size, [&](u32 off, u32 sz) {
        auto v = b.build_load(ctx->get_int_type(8 * sz), addr(b, a, off), false,
                              I->Volatile);
        auto w = b.build_zext(v, i64);
        if (off != 0) {
          w = b.build_shl(w, c64(8 * off));
        }
        acc = acc ? bin(b, BinOp::Or, *acc, w) : w;
      });
      r.push_back(*acc);
    }
    return r;
  }

  void store_limbs(fir::Builder &b, fir::Instr I, const Limbs &x, u32 width) {
    const u32 n = (width + 63) / 64;
    const u32 bytes = (width + 7) / 8;
    auto ptr = I->args[0];
    for (u32 i = 0; i < n; i++) {
      const u32 size = std::min(8u, bytes - 8 * i);
      const auto a = addr(b, ptr, 8ull * i);
      if (size == 8) {
        b.build_store(a, x[i], false, I->Volatile);
        continue;
      }
      for_pieces(size, [&](u32 off, u32 sz) {
        auto v = off == 0 ? x[i] : b.build_lshr(x[i], c64(8 * off));
        b.build_store(addr(b, a, off),
                      b.build_itrunc(v, ctx->get_int_type(8 * sz)), false,
                      I->Volatile);
      });
    }
  }

  void set(fir::ValueR v, Limbs l) { map.insert_or_assign(v, std::move(l)); }

  void finish(fir::Instr I, Limbs l) {
    set(I.result(), std::move(l));
    dead.push_back(I);
  }
  void finish_scalar(fir::Instr I, fir::ValueR v) {
    I.result().replace_all_uses(v);
    dead.push_back(I);
  }

  [[noreturn]] void unsupported(fir::Instr I) {
    fmt::println(stderr, "LegalizeInts: unsupported {} on a wide integer",
                 I->get_name());
    IMPL("wide integer operation");
    std::abort();
  }

  void run_binary(fir::Instr I, fir::Builder &b) {
    const u32 width = I.result_type()->get_bitwidth();
    const auto op = static_cast<BinOp>(I->subtype);
    const auto x = get(I->args[0]);
    switch (op) {
      case BinOp::And:
      case BinOp::Or:
      case BinOp::Xor: {
        auto y = get(I->args[1]);
        Limbs r;
        for (size_t i = 0; i < x.size(); i++) {
          r.push_back(bin(b, op, x[i], y[i]));
        }
        return finish(I, std::move(r));
      }
      case BinOp::IntAdd:
        return finish(I, add_limbs(b, x, get(I->args[1])));
      case BinOp::IntSub:
        return finish(I, sub_limbs(b, x, get(I->args[1])));
      case BinOp::IntMul:
        return finish(I, mul_limbs(b, x, get(I->args[1])));
      case BinOp::Shl:
      case BinOp::Shr:
      case BinOp::AShr:
        return finish(I,
                      shift_limbs(b, op, x, amount_i64(b, I->args[1]), width));
      case BinOp::IntSDiv:
      case BinOp::IntUDiv:
      case BinOp::IntSRem:
      case BinOp::IntURem:
        return finish(I, divrem(b, op, x, get(I->args[1]), width));
      default:
        unsupported(I);
    }
  }

  void run_unary(fir::Instr I, fir::Builder &b) {
    const auto op = static_cast<fir::UnaryInstrSubType>(I->subtype);
    const auto x = get(I->args[0]);
    if (op == fir::UnaryInstrSubType::Not) {
      Limbs r;
      for (auto l : x) {
        r.push_back(bin(b, BinOp::Xor, l, c64(~u64{0})));
      }
      return finish(I, std::move(r));
    }
    if (op == fir::UnaryInstrSubType::IntNeg) {
      return finish(I, sub_limbs(b, Limbs(x.size(), c64(0)), x));
    }
    unsupported(I);
  }

  fir::ValueR wide_cmp(fir::Builder &b, fir::Instr I, Cmp p, const Limbs &a,
                       const Limbs &c, u32 width) {
    const bool is_signed =
        p == Cmp::SLT || p == Cmp::SLE || p == Cmp::SGT || p == Cmp::SGE;
    const bool is_eq = p == Cmp::EQ || p == Cmp::NE;
    auto x = norm(b, a, width, is_signed);
    auto y = norm(b, c, width, is_signed);
    if (is_eq) {
      auto acc = bin(b, BinOp::Xor, x[0], y[0]);
      for (size_t i = 1; i < x.size(); i++) {
        acc = bin(b, BinOp::Or, acc, bin(b, BinOp::Xor, x[i], y[i]));
      }
      return cmp(b, p, acc, c64(0));
    }
    Cmp low = p;
    Cmp strict_u = Cmp::ULT;
    Cmp strict_s = Cmp::SLT;
    switch (p) {
      case Cmp::SLT:
        low = Cmp::ULT;
        break;
      case Cmp::SLE:
        low = Cmp::ULE;
        break;
      case Cmp::SGT:
        low = Cmp::UGT;
        strict_u = Cmp::UGT;
        strict_s = Cmp::SGT;
        break;
      case Cmp::SGE:
        low = Cmp::UGE;
        strict_u = Cmp::UGT;
        strict_s = Cmp::SGT;
        break;
      case Cmp::ULT:
      case Cmp::ULE:
        break;
      case Cmp::UGT:
      case Cmp::UGE:
        strict_u = Cmp::UGT;
        break;
      default:
        unsupported(I);
    }
    auto res = cmp(b, low, x[0], y[0]);
    for (size_t i = 1; i < x.size(); i++) {
      const bool top = i + 1 == x.size();
      auto here = cmp(b, top && is_signed ? strict_s : strict_u, x[i], y[i]);
      auto same = cmp(b, Cmp::EQ, x[i], y[i]);
      res = b.build_select(ctx->get_int_type(1), same, res, here);
    }
    return res;
  }

  void run_icmp(fir::Instr I, fir::Builder &b) {
    const u32 width = I->args[0].get_type()->get_bitwidth();
    finish_scalar(I, wide_cmp(b, I, static_cast<Cmp>(I->subtype),
                              get(I->args[0]), get(I->args[1]), width));
  }

  void run_intrinsic(fir::Instr I, fir::Builder &b) {
    using Intr = fir::IntrinsicSubType;
    Cmp pred = Cmp::INVALID;
    switch (static_cast<Intr>(I->subtype)) {
      case Intr::UMin:
        pred = Cmp::ULT;
        break;
      case Intr::UMax:
        pred = Cmp::UGT;
        break;
      case Intr::SMin:
        pred = Cmp::SLT;
        break;
      case Intr::SMax:
        pred = Cmp::SGT;
        break;
      default:
        unsupported(I);
    }
    const u32 width = I.result_type()->get_bitwidth();
    auto x = get(I->args[0]);
    auto y = get(I->args[1]);
    auto first = wide_cmp(b, I, pred, x, y, width);
    Limbs r;
    for (size_t i = 0; i < x.size(); i++) {
      r.push_back(b.build_select(i64, first, x[i], y[i]));
    }
    finish(I, std::move(r));
  }

  void run_select(fir::Instr I, fir::Builder &b) {
    auto t = get(I->args[1]);
    auto f = get(I->args[2]);
    Limbs r;
    for (size_t i = 0; i < t.size(); i++) {
      r.push_back(b.build_select(i64, I->args[0], t[i], f[i]));
    }
    finish(I, std::move(r));
  }

  void run_conversion(fir::Instr I, fir::Builder &b) {
    const auto src_ty = I->args[0].get_type();
    const auto dst_ty = I.result_type();
    const u32 n_src = n_limbs(src_ty);
    const u32 n_dst = n_limbs(dst_ty);
    if (I->is(fir::InstrType::ITrunc)) {
      auto x = get(I->args[0]);
      if (n_dst == 0) {
        auto v = x[0];
        if (dst_ty->get_bitwidth() != 64) {
          v = b.build_itrunc(v, dst_ty);
        }
        return finish_scalar(I, v);
      }
      x.resize(n_dst, c64(0));
      return finish(I, std::move(x));
    }
    const bool is_signed = I->is(fir::InstrType::SExt);
    ASSERT(n_dst != 0);
    Limbs r;
    if (n_src == 0) {
      r.push_back(widen_narrow(b, I->args[0], is_signed));
    } else {
      r = norm(b, get(I->args[0]), src_ty->get_bitwidth(), is_signed);
    }
    fir::ValueR pad = c64(0);
    if (is_signed) {
      pad = b.build_ashr(r.back(), c64(63));
    }
    r.resize(n_dst, pad);
    finish(I, std::move(r));
  }

  void run_call(fir::Instr I, fir::Builder &b) {
    const auto &old_ty = I->extra_type->as_func();
    auto new_ty = legal_func_type(ctx, old_ty);
    auto nc = b.insert_copy(I);
    nc.clear_args();
    nc.add_arg(I->args[0]);
    for (size_t i = 1; i < I->args.size(); i++) {
      if (value_is_wide(I->args[i])) {
        for (auto l : get(I->args[i])) {
          nc.add_arg(l);
        }
      } else {
        nc.add_arg(I->args[i]);
      }
    }
    nc->extra_type = new_ty;
    nc->set_result_types(new_ty->as_func().return_types);
    u8 pos = 0;
    for (u8 i = 0; i < I.n_results(); i++) {
      auto old_res = I.result(i);
      const u32 n = n_limbs(I.result_type(i));
      if (n == 0) {
        old_res.replace_all_uses(nc.result(pos++));
        continue;
      }
      Limbs l;
      for (u32 k = 0; k < n; k++) {
        l.push_back(nc.result(pos++));
      }
      set(old_res, std::move(l));
    }
    dead.push_back(I);
  }

  void run_return(fir::Instr I, fir::Builder &b) {
    TVec<fir::ValueR> vals;
    for (auto a : I->args) {
      if (value_is_wide(a)) {
        for (auto l : get(a)) {
          vals.push_back(l);
        }
      } else {
        vals.push_back(a);
      }
    }
    b.build_return(std::span<const fir::ValueR>{vals});
    dead.push_back(I);
  }

  void run_instr(fir::Instr I) {
    if (I->is(fir::InstrType::AllocaInstr)) {
      if (I->extra_type.is_valid() && n_limbs(I->extra_type) != 0) {
        I->extra_type = fir::TypeR{fir::TypeR::invalid()};
        changed = true;
      }
      return;
    }
    if ((I->is(fir::IntrinsicSubType::Memcpy) ||
         I->is(fir::IntrinsicSubType::Memmove)) &&
        I->extra_type.is_valid() && n_limbs(I->extra_type) != 0) {
      const u32 w = I->extra_type->get_bitwidth();
      if (w % 64 != 0) {
        unsupported(I);
      }
      fir::Builder b{I};
      I.replace_arg(2, bin(b, BinOp::IntMul, I->args[2], c64(w / 64)));
      I->extra_type = i64;
      changed = true;
      return;
    }
    bool wide = false;
    for (u8 i = 0; i < I.n_results(); i++) {
      wide |= n_limbs(I.result_type(i)) != 0;
    }
    for (auto a : I->args) {
      wide |= value_is_wide(a);
    }
    if (!wide) {
      return;
    }
    changed = true;
    fir::Builder b{I};
    switch (I->instr_type) {
      case fir::InstrType::BinaryInstr:
        return run_binary(I, b);
      case fir::InstrType::UnaryInstr:
        return run_unary(I, b);
      case fir::InstrType::ICmp:
        return run_icmp(I, b);
      case fir::InstrType::SelectInstr:
        return run_select(I, b);
      case fir::InstrType::Intrinsic:
        return run_intrinsic(I, b);
      case fir::InstrType::ITrunc:
      case fir::InstrType::ZExt:
      case fir::InstrType::SExt:
        return run_conversion(I, b);
      case fir::InstrType::LoadInstr: {
        if (I->Atomic) {
          unsupported(I);
        }
        return finish(I, load_limbs(b, I, I.result_type()->get_bitwidth()));
      }
      case fir::InstrType::StoreInstr: {
        if (I->Atomic) {
          unsupported(I);
        }
        store_limbs(b, I, get(I->args[1]),
                    I->args[1].get_type()->get_bitwidth());
        dead.push_back(I);
        return;
      }
      case fir::InstrType::CallInstr:
        return run_call(I, b);
      case fir::InstrType::ReturnInstr:
        return run_return(I, b);
      default:
        unsupported(I);
    }
  }

  TVec<fir::BasicBlock> block_order() {
    TVec<fir::BasicBlock> post;
    TMap<fir::BasicBlock, u8> seen;
    TVec<std::pair<fir::BasicBlock, size_t>> stack;
    auto push = [&](fir::BasicBlock bb) {
      if (seen.emplace(bb, 1).second) {
        stack.emplace_back(bb, 0);
      }
    };
    push(func.get_entry());
    while (!stack.empty()) {
      auto &[bb, next] = stack.back();
      auto term = bb->get_terminator();
      if (term.is_valid() && next < term->bbs.size()) {
        auto succ = term->bbs[next++].bb;
        push(succ);
        continue;
      }
      post.push_back(bb);
      stack.pop_back();
    }
    TVec<fir::BasicBlock> order(post.rbegin(), post.rend());
    for (auto bb : func.basic_blocks) {
      if (!seen.contains(bb)) {
        order.push_back(bb);
      }
    }
    return order;
  }

  void make_block_args(fir::BasicBlock bb) {
    TVec<WideArg> wide;
    const auto old_args = bb->args;
    for (u32 i = 0; i < old_args.size(); i++) {
      const u32 n = n_limbs(old_args[i]->get_type());
      if (n == 0) {
        continue;
      }
      WideArg w{i, {}};
      for (u32 k = 0; k < n; k++) {
        auto arg = ctx->storage.insert_bb_arg(bb, i64);
        bb.add_arg(arg);
        w.limbs.push_back(fir::ValueR{arg});
      }
      set(fir::ValueR{old_args[i]}, w.limbs);
      wide.push_back(std::move(w));
    }
    if (!wide.empty()) {
      wide_args.insert_or_assign(bb, std::move(wide));
    }
  }

  // rewrites the operands of every branch into bb and drops the old arguments
  void fix_edges(fir::BasicBlock bb, const TVec<WideArg> &wide) {
    TVec<fir::Instr> terms;
    for (const auto &u : bb->get_uses()) {
      if (u.type == fir::UseType::BB &&
          std::ranges::find(terms, u.user) == terms.end()) {
        terms.push_back(u.user);
      }
    }
    for (auto term : terms) {
      for (u16 e = 0; e < term->bbs.size(); e++) {
        if (!(term->bbs[e].bb == bb)) {
          continue;
        }
        const auto old_vals = term->bbs[e].args;
        for (size_t k = wide.size(); k-- > 0;) {
          term.remove_bb_arg(e, wide[k].index);
        }
        for (const auto &w : wide) {
          for (auto l : get(old_vals[w.index])) {
            term.add_bb_arg(e, l);
          }
        }
      }
    }
  }

 public:
  Expander(fir::Context &ctx, fir::Function &func)
      : ctx(ctx), func(func), i64(ctx->get_int_type(64)) {}

  bool run() {
    auto order = block_order();
    for (auto bb : order) {
      make_block_args(bb);
    }
    for (auto bb : order) {
      const auto instrs = bb->instructions;
      for (auto I : instrs) {
        run_instr(I);
      }
    }
    const auto entry = func.get_entry();
    for (auto bb : order) {
      auto it = wide_args.find(bb);
      if (it == wide_args.end()) {
        continue;
      }
      changed = true;
      if (!(bb == entry)) {
        fix_edges(bb, it->second);
      }
    }
    for (size_t i = dead.size(); i-- > 0;) {
      dead[i].destroy();
    }
    for (auto bb : order) {
      auto it = wide_args.find(bb);
      if (it == wide_args.end()) {
        continue;
      }
      // the entry block keeps the limbs at the position of the wide argument
      // (they are the function arguments), other blocks append them like the
      // edges do
      const auto &wide = it->second;
      size_t n_limbs_total = 0;
      for (const auto &w : wide) {
        n_limbs_total += w.limbs.size();
      }
      const auto all = bb->args;
      const size_t n_old = all.size() - n_limbs_total;
      IRVec<fir::BBArgument> args;
      IRVec<fir::BBArgument> tail;
      size_t next_new = n_old;
      for (size_t i = 0; i < n_old; i++) {
        const WideArg *w = nullptr;
        for (const auto &c : wide) {
          if (c.index == i) {
            w = &c;
          }
        }
        if (w == nullptr) {
          args.push_back(all[i]);
          continue;
        }
        for (size_t k = 0; k < w->limbs.size(); k++) {
          (bb == entry ? args : tail).push_back(all[next_new++]);
        }
      }
      for (auto t : tail) {
        args.push_back(t);
      }
      bb->args = std::move(args);
    }
    return changed;
  }
};

bool function_is_wide(const fir::Function &f) {
  if (func_type_is_wide(f.func_ty->as_func())) {
    return true;
  }
  for (auto bb : f.basic_blocks) {
    for (auto arg : bb->args) {
      if (n_limbs(arg->get_type()) != 0) {
        return true;
      }
    }
    for (auto I : bb->instructions) {
      for (u8 i = 0; i < I.n_results(); i++) {
        if (n_limbs(I.result_type(i)) != 0) {
          return true;
        }
      }
      for (auto a : I->args) {
        if (value_is_wide(a)) {
          return true;
        }
      }
      if ((I->is(fir::InstrType::AllocaInstr) ||
           I->is(fir::IntrinsicSubType::Memcpy) ||
           I->is(fir::IntrinsicSubType::Memmove)) &&
          I->extra_type.is_valid() && n_limbs(I->extra_type) != 0) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

PreservedAnalysis LegalizeInts::apply(fir::Context &ctx, JobSheduler * /*shed*/,
                                      AnalysisManager &analyMan) {
  ZoneScopedNC("LegalizeInts", COLOR_OPTIMF);
  const auto funcs = ctx->storage.functions.all();
  TVec<fir::Function *> work(funcs.begin(), funcs.end());
  for (auto *f : work) {
    if (!function_is_wide(*f)) {
      continue;
    }
    const auto old_ty = f->func_ty->as_func();
    const bool sig_wide = func_type_is_wide(old_ty);
    if (!f->is_decl()) {
      Expander{ctx, *f}.run();
    }
    if (sig_wide) {
      f->func_ty = legal_func_type(ctx, old_ty);
      analyMan.touched_signature(*f);
    } else {
      analyMan.touched_body(*f);
    }
  }
  return PreservedAnalysis::all();
}

}  // namespace foptim::optim
