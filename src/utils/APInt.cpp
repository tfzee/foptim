#include "utils/APInt.hpp"

#include <algorithm>
#include <bit>
#include <vector>

namespace foptim {

namespace {
u64 add_n(u64 *r, const u64 *a, const u64 *b, u32 n) {
  u64 carry = 0;
  for (u32 i = 0; i < n; i++) {
    const u128 t = u128{a[i]} + b[i] + carry;
    r[i] = static_cast<u64>(t);
    carry = static_cast<u64>(t >> 64);
  }
  return carry;
}

u64 sub_n(u64 *r, const u64 *a, const u64 *b, u32 n) {
  u64 borrow = 0;
  for (u32 i = 0; i < n; i++) {
    const u128 t = u128{a[i]} - b[i] - borrow;
    r[i] = static_cast<u64>(t);
    borrow = static_cast<u64>(t >> 64) & 1;
  }
  return borrow;
}

// low n words of a * b, r must not alias a or b
void mul_n(u64 *r, const u64 *a, const u64 *b, u32 n) {
  std::fill(r, r + n, u64{0});
  for (u32 i = 0; i < n; i++) {
    u64 carry = 0;
    for (u32 j = 0; i + j < n; j++) {
      const u128 t = u128{a[i]} * b[j] + r[i + j] + carry;
      r[i + j] = static_cast<u64>(t);
      carry = static_cast<u64>(t >> 64);
    }
  }
}

int ucmp_n(const u64 *a, const u64 *b, u32 n) {
  for (u32 i = n; i-- > 0;) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

void shl_n(u64 *r, const u64 *a, u32 n, u32 amt) {
  const u32 ws = amt / 64;
  const u32 bs = amt % 64;
  for (u32 i = n; i-- > 0;) {
    u64 v = 0;
    if (i >= ws) {
      v = a[i - ws] << bs;
      if (bs != 0 && i > ws) {
        v |= a[i - ws - 1] >> (64 - bs);
      }
    }
    r[i] = v;
  }
}

// fill is the value shifted in from the top (0 or ~0)
void shr_n(u64 *r, const u64 *a, u32 n, u32 amt, u64 fill) {
  const u32 ws = amt / 64;
  const u32 bs = amt % 64;
  for (u32 i = 0; i < n; i++) {
    const u64 lo = i + ws < n ? a[i + ws] : fill;
    const u64 hi = i + ws + 1 < n ? a[i + ws + 1] : fill;
    r[i] = bs == 0 ? lo : (lo >> bs) | (hi << (64 - bs));
  }
}

// Knuth algorithm D on 32 bit limbs. u has m limbs, v has n limbs with
// v[n-1] != 0, m >= n. q gets m-n+1 limbs, r gets n limbs.
void divmnu(u32 *q, u32 *r, const u32 *u, const u32 *v, int m, int n) {
  constexpr u64 B = u64{1} << 32;
  if (n == 1) {
    u64 k = 0;
    for (int j = m - 1; j >= 0; j--) {
      const u64 cur = (k << 32) | u[j];
      q[j] = static_cast<u32>(cur / v[0]);
      k = cur % v[0];
    }
    r[0] = static_cast<u32>(k);
    return;
  }
  const int s = std::countl_zero(v[n - 1]);
  std::vector<u32> vn(n);
  std::vector<u32> un(m + 1);
  for (int i = n - 1; i > 0; i--) {
    vn[i] = static_cast<u32>((u64{v[i]} << s) | (u64{v[i - 1]} >> (32 - s)));
  }
  vn[0] = static_cast<u32>(u64{v[0]} << s);
  un[m] = static_cast<u32>(u64{u[m - 1]} >> (32 - s));
  for (int i = m - 1; i > 0; i--) {
    un[i] = static_cast<u32>((u64{u[i]} << s) | (u64{u[i - 1]} >> (32 - s)));
  }
  un[0] = static_cast<u32>(u64{u[0]} << s);

  for (int j = m - n; j >= 0; j--) {
    const u64 num = (u64{un[j + n]} << 32) | un[j + n - 1];
    u64 qhat = num / vn[n - 1];
    u64 rhat = num - qhat * vn[n - 1];
    while (qhat >= B || qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
      qhat--;
      rhat += vn[n - 1];
      if (rhat >= B) {
        break;
      }
    }
    i64 borrow = 0;
    for (int i = 0; i < n; i++) {
      const u64 p = qhat * vn[i];
      const i64 t = static_cast<i64>(un[i + j]) - borrow -
                    static_cast<i64>(p & 0xFFFFFFFF);
      un[i + j] = static_cast<u32>(t);
      borrow = static_cast<i64>(p >> 32) - (t >> 32);
    }
    const i64 t = static_cast<i64>(un[j + n]) - borrow;
    un[j + n] = static_cast<u32>(t);
    q[j] = static_cast<u32>(qhat);
    if (t < 0) {
      q[j]--;
      u64 carry = 0;
      for (int i = 0; i < n; i++) {
        const u64 t2 = u64{un[i + j]} + vn[i] + carry;
        un[i + j] = static_cast<u32>(t2);
        carry = t2 >> 32;
      }
      un[j + n] += static_cast<u32>(carry);
    }
  }
  for (int i = 0; i < n; i++) {
    r[i] = static_cast<u32>((u64{un[i]} >> s) | (u64{un[i + 1]} << (32 - s)));
  }
}

std::vector<u32> to_limbs(const u64 *w, u32 n) {
  std::vector<u32> l(2 * n);
  for (u32 i = 0; i < n; i++) {
    l[2 * i] = static_cast<u32>(w[i]);
    l[2 * i + 1] = static_cast<u32>(w[i] >> 32);
  }
  return l;
}

u32 trim(const std::vector<u32> &l) {
  u32 n = static_cast<u32>(l.size());
  while (n > 0 && l[n - 1] == 0) {
    n--;
  }
  return n;
}
}  // namespace

APInt APInt::from_i64(u32 bits, i64 v) {
  APInt r(bits, ZeroTag{});
  u64 *d = r.data();
  const u32 n = r.num_words();
  d[0] = static_cast<u64>(v);
  for (u32 i = 1; i < n; i++) {
    d[i] = v < 0 ? ~u64{0} : 0;
  }
  r.clear_unused();
  return r;
}

APInt APInt::from_u128(u32 bits, u128 v) {
  APInt r(bits, ZeroTag{});
  u64 *d = r.data();
  d[0] = static_cast<u64>(v);
  if (r.num_words() > 1) {
    d[1] = static_cast<u64>(v >> 64);
  }
  r.clear_unused();
  return r;
}

APInt APInt::from_i128(u32 bits, i128 v) {
  APInt r(bits, ZeroTag{});
  u64 *d = r.data();
  const u32 n = r.num_words();
  d[0] = static_cast<u64>(v);
  if (n > 1) {
    d[1] = static_cast<u64>(static_cast<u128>(v) >> 64);
  }
  for (u32 i = 2; i < n; i++) {
    d[i] = v < 0 ? ~u64{0} : 0;
  }
  r.clear_unused();
  return r;
}

APInt APInt::from_words(u32 bits, const u64 *words, u32 count) {
  APInt r(bits, ZeroTag{});
  const u32 n = std::min(count, r.num_words());
  std::copy(words, words + n, r.data());
  r.clear_unused();
  return r;
}

APInt APInt::all_ones(u32 bits) {
  APInt r(bits, ZeroTag{});
  std::fill(r.data(), r.data() + r.num_words(), ~u64{0});
  r.clear_unused();
  return r;
}

APInt APInt::signed_max(u32 bits) {
  APInt r = all_ones(bits);
  r.set_bit(bits - 1, false);
  return r;
}

APInt APInt::signed_min(u32 bits) {
  APInt r(bits, ZeroTag{});
  r.set_bit(bits - 1);
  return r;
}

u128 APInt::to_u128() const {
  if (is_single()) {
    return val_;
  }
  return get128();
}

i128 APInt::to_i128() const {
  if (bits_ >= 128) {
    return static_cast<i128>(to_u128());
  }
  const u32 sh = 128 - bits_;
  return static_cast<i128>(to_u128() << sh) >> sh;
}

bool APInt::is_zero_wide() const {
  for (u32 i = 0, n = num_words(); i < n; i++) {
    if (big_[i] != 0) {
      return false;
    }
  }
  return true;
}

bool APInt::is_one() const {
  const u64 *d = data();
  if (d[0] != 1) {
    return false;
  }
  for (u32 i = 1, n = num_words(); i < n; i++) {
    if (d[i] != 0) {
      return false;
    }
  }
  return true;
}

u32 APInt::popcount() const {
  const u64 *d = data();
  u32 c = 0;
  for (u32 i = 0, n = num_words(); i < n; i++) {
    c += static_cast<u32>(std::popcount(d[i]));
  }
  return c;
}

u32 APInt::clz() const {
  const u64 *d = data();
  const u32 n = num_words();
  u32 c = 0;
  for (u32 i = n; i-- > 0;) {
    if (d[i] != 0) {
      c += static_cast<u32>(std::countl_zero(d[i]));
      return c - (n * 64 - bits_);
    }
    c += 64;
  }
  return bits_;
}

u32 APInt::ctz() const {
  const u64 *d = data();
  for (u32 i = 0, n = num_words(); i < n; i++) {
    if (d[i] != 0) {
      return i * 64 + static_cast<u32>(std::countr_zero(d[i]));
    }
  }
  return bits_;
}

APInt APInt::not_wide() const {
  APInt r(bits_, ZeroTag{});
  for (u32 i = 0, n = num_words(); i < n; i++) {
    r.big_[i] = ~big_[i];
  }
  r.clear_unused();
  return r;
}

// Everything below handles widths above 64. Up to 128 bits the work is done on
// a native 128 bit integer, wider values use the word loops.

APInt APInt::add_wide(const APInt &o) const {
  if (bits_ <= 128) {
    return from_u128(bits_, get128() + o.get128());
  }
  APInt r(bits_, ZeroTag{});
  add_n(r.big_, big_, o.big_, num_words());
  r.clear_unused();
  return r;
}

APInt APInt::sub_wide(const APInt &o) const {
  if (bits_ <= 128) {
    return from_u128(bits_, get128() - o.get128());
  }
  APInt r(bits_, ZeroTag{});
  sub_n(r.big_, big_, o.big_, num_words());
  r.clear_unused();
  return r;
}

APInt APInt::mul_wide(const APInt &o) const {
  if (bits_ <= 128) {
    return from_u128(bits_, get128() * o.get128());
  }
  APInt r(bits_, ZeroTag{});
  mul_n(r.big_, big_, o.big_, num_words());
  r.clear_unused();
  return r;
}

APInt APInt::and_wide(const APInt &o) const {
  APInt r(bits_, ZeroTag{});
  for (u32 i = 0, n = num_words(); i < n; i++) {
    r.big_[i] = big_[i] & o.big_[i];
  }
  return r;
}

APInt APInt::or_wide(const APInt &o) const {
  APInt r(bits_, ZeroTag{});
  for (u32 i = 0, n = num_words(); i < n; i++) {
    r.big_[i] = big_[i] | o.big_[i];
  }
  return r;
}

APInt APInt::xor_wide(const APInt &o) const {
  APInt r(bits_, ZeroTag{});
  for (u32 i = 0, n = num_words(); i < n; i++) {
    r.big_[i] = big_[i] ^ o.big_[i];
  }
  return r;
}

APInt APInt::shl_wide(u32 amt) const {
  if (amt >= bits_) {
    return zero(bits_);
  }
  if (bits_ <= 128) {
    return from_u128(bits_, get128() << amt);
  }
  APInt r(bits_, ZeroTag{});
  shl_n(r.big_, big_, num_words(), amt);
  r.clear_unused();
  return r;
}

APInt APInt::lshr_wide(u32 amt) const {
  if (amt >= bits_) {
    return zero(bits_);
  }
  if (bits_ <= 128) {
    return from_u128(bits_, get128() >> amt);
  }
  APInt r(bits_, ZeroTag{});
  shr_n(r.big_, big_, num_words(), amt, 0);
  return r;
}

APInt APInt::ashr_wide(u32 amt) const {
  amt = std::min(amt, bits_ - 1);
  if (bits_ <= 128) {
    return from_i128(bits_, to_i128() >> amt);
  }
  // sign extend into the unused top bits, shift, then drop them again
  APInt ext = is_neg() ? sext(num_words() * 64) : zext(num_words() * 64);
  APInt r(bits_, ZeroTag{});
  shr_n(r.big_, ext.data(), num_words(), amt, is_neg() ? ~u64{0} : 0);
  r.clear_unused();
  return r;
}

int APInt::ucmp_wide(const APInt &o) const {
  if (bits_ <= 128) {
    const u128 a = get128();
    const u128 b = o.get128();
    return static_cast<int>(a > b) - static_cast<int>(a < b);
  }
  return ucmp_n(big_, o.big_, num_words());
}

int APInt::scmp_wide(const APInt &o) const {
  const bool na = is_neg();
  const bool nb = o.is_neg();
  if (na != nb) {
    return na ? -1 : 1;
  }
  return ucmp_wide(o);
}

bool APInt::eq_wide(const APInt &o) const {
  return ucmp_n(big_, o.big_, num_words()) == 0;
}

void APInt::udivrem_wide(const APInt &a, const APInt &b, APInt *q, APInt *r) {
  ASSERT(!b.is_zero());
  const u32 bits = a.bits_;
  if (bits <= 128) {
    const u128 x = a.get128();
    const u128 y = b.get128();
    if (q != nullptr) {
      *q = from_u128(bits, x / y);
    }
    if (r != nullptr) {
      *r = from_u128(bits, x % y);
    }
    return;
  }
  const auto ul = to_limbs(a.big_, a.num_words());
  const auto vl = to_limbs(b.big_, b.num_words());
  const u32 m = trim(ul);
  const u32 n = trim(vl);
  APInt qq = zero(bits);
  APInt rr = zero(bits);
  if (m < n) {
    rr = a;
  } else {
    std::vector<u32> ql(m - n + 1);
    std::vector<u32> rl(n);
    divmnu(ql.data(), rl.data(), ul.data(), vl.data(), static_cast<int>(m),
           static_cast<int>(n));
    for (u32 i = 0; i < ql.size(); i++) {
      qq.big_[i / 2] |= u64{ql[i]} << (32 * (i % 2));
    }
    for (u32 i = 0; i < n; i++) {
      rr.big_[i / 2] |= u64{rl[i]} << (32 * (i % 2));
    }
  }
  if (q != nullptr) {
    *q = static_cast<APInt &&>(qq);
  }
  if (r != nullptr) {
    *r = static_cast<APInt &&>(rr);
  }
}

APInt APInt::udiv_wide(const APInt &o) const {
  APInt q;
  udivrem_wide(*this, o, &q, nullptr);
  return q;
}

APInt APInt::urem_wide(const APInt &o) const {
  APInt r;
  udivrem_wide(*this, o, nullptr, &r);
  return r;
}

APInt APInt::sdiv_wide(const APInt &o) const {
  if (bits_ <= 128) {
    const i128 a = to_i128();
    const i128 b = o.to_i128();
    return from_u128(bits_, b == -1 ? u128{0} - static_cast<u128>(a)
                                    : static_cast<u128>(a / b));
  }
  const bool neg = is_neg() != o.is_neg();
  APInt q;
  udivrem_wide(is_neg() ? -*this : *this, o.is_neg() ? -o : o, &q, nullptr);
  return neg ? -q : q;
}

APInt APInt::srem_wide(const APInt &o) const {
  if (bits_ <= 128) {
    const i128 a = to_i128();
    const i128 b = o.to_i128();
    return from_u128(bits_, b == -1 ? u128{0} : static_cast<u128>(a % b));
  }
  APInt r;
  udivrem_wide(is_neg() ? -*this : *this, o.is_neg() ? -o : o, nullptr, &r);
  return is_neg() ? -r : r;
}

APInt APInt::uadd_ov(const APInt &o, bool &ov) const {
  APInt r = *this + o;
  ov = r.ult(*this);
  return r;
}

APInt APInt::usub_ov(const APInt &o, bool &ov) const {
  ov = ult(o);
  return *this - o;
}

APInt APInt::umul_ov(const APInt &o, bool &ov) const {
  ASSERT(bits_ == o.bits_);
  if (is_single()) {
    const u128 p = u128{val_} * o.val_;
    ov = (p >> bits_) != 0;
    return APInt(bits_, static_cast<u64>(p));
  }
  const APInt p = zext(2 * bits_) * o.zext(2 * bits_);
  ov = p.active_bits() > bits_;
  return p.trunc(bits_);
}

APInt APInt::sadd_ov(const APInt &o, bool &ov) const {
  APInt r = *this + o;
  ov = is_neg() == o.is_neg() && r.is_neg() != is_neg();
  return r;
}

APInt APInt::ssub_ov(const APInt &o, bool &ov) const {
  APInt r = *this - o;
  ov = is_neg() != o.is_neg() && r.is_neg() != is_neg();
  return r;
}

APInt APInt::smul_ov(const APInt &o, bool &ov) const {
  ASSERT(bits_ == o.bits_);
  if (is_single()) {
    const i128 p = i128{sext64()} * o.sext64();
    const i128 hi = (i128{1} << (bits_ - 1)) - 1;
    const i128 lo = -hi - 1;
    ov = p > hi || p < lo;
    return APInt(bits_, static_cast<u64>(p));
  }
  const APInt p = sext(2 * bits_) * o.sext(2 * bits_);
  const APInt t = p.trunc(bits_);
  ov = t.sext(2 * bits_) != p;
  return t;
}

APInt APInt::zext_wide(u32 new_bits) const {
  APInt r(new_bits, ZeroTag{});
  std::copy(data(), data() + num_words(), r.data());
  return r;
}

APInt APInt::sext_wide(u32 new_bits) const {
  APInt r = zext_wide(new_bits);
  if (is_neg()) {
    u64 *d = r.data();
    const u32 rem = bits_ & 63;
    const u32 first = bits_ / 64;
    if (rem != 0) {
      d[first] |= ~u64{0} << rem;
    }
    for (u32 i = first + (rem != 0 ? 1 : 0); i < r.num_words(); i++) {
      d[i] = ~u64{0};
    }
    r.clear_unused();
  }
  return r;
}

APInt APInt::trunc_wide(u32 new_bits) const {
  APInt r(new_bits, ZeroTag{});
  std::copy(data(), data() + r.num_words(), r.data());
  r.clear_unused();
  return r;
}

u64 APInt::hash() const {
  u64 h = bits_ * u64{0x9E3779B97F4A7C15};
  const u64 *d = data();
  for (u32 i = 0, n = num_words(); i < n; i++) {
    h = (h ^ d[i]) * u64{0xFF51AFD7ED558CCD};
    h ^= h >> 32;
  }
  return h;
}

std::string APInt::to_string(bool is_signed, u32 radix) const {
  ASSERT(radix == 2 || radix == 10 || radix == 16);
  const bool neg = is_signed && is_neg();
  const APInt mag = neg ? -*this : *this;
  std::string s;
  if (radix == 10) {
    std::vector<u32> l = to_limbs(mag.data(), mag.num_words());
    u32 n = trim(l);
    while (n > 0) {
      u64 rem = 0;
      for (u32 i = n; i-- > 0;) {
        const u64 cur = (rem << 32) | l[i];
        l[i] = static_cast<u32>(cur / 1000000000);
        rem = cur % 1000000000;
      }
      while (n > 0 && l[n - 1] == 0) {
        n--;
      }
      for (int k = 0; k < 9 && (n > 0 || rem > 0); k++) {
        s.push_back(static_cast<char>('0' + rem % 10));
        rem /= 10;
      }
    }
  } else {
    const u32 shift = radix == 16 ? 4 : 1;
    for (u32 i = 0; i < mag.bits_; i += shift) {
      u32 digit = 0;
      for (u32 k = 0; k < shift && i + k < mag.bits_; k++) {
        digit |= static_cast<u32>(mag.bit(i + k)) << k;
      }
      s.push_back("0123456789abcdef"[digit]);
    }
    while (s.size() > 1 && s.back() == '0') {
      s.pop_back();
    }
  }
  if (s.empty()) {
    s = "0";
  }
  if (neg) {
    s.push_back('-');
  }
  std::reverse(s.begin(), s.end());
  return s;
}

}  // namespace foptim
