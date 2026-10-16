#pragma once
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef __BMI2__
#include <immintrin.h>
#endif

#include "utils/todo.hpp"
#include "utils/types.hpp"

namespace foptim {

// Arbitrary width two's complement integer. The signedness lives in the
// operation, not in the value. Values up to 64 bits are stored inline and take
// a single word fast path that compiles to the plain machine operation, wider
// ones live on the heap. Unused high bits are always zero.
// Operands of binary operations must have the same width. Division by zero is
// a precondition violation. Shifts by an amount >= the width give zero (ashr
// gives the sign fill), callers that need poison have to check the amount.
class APInt {
  u32 bits_;
  union {
    u64 val_;
    u64 *big_;
  };

  static constexpr u32 words_for(u32 bits) { return (bits + 63) / 64; }
  // low bits of a word, bits is 1 to 64
  static u64 mask64(u32 bits) {
#ifdef __BMI2__
    return _bzhi_u64(~u64{0}, bits);
#else
    return ~u64{0} >> (64 - bits);
#endif
  }

  [[nodiscard]] bool is_single() const { return bits_ <= 64; }

  // only for 65 to 128 bit values
  [[nodiscard]] u128 get128() const {
    return u128{big_[0]} | (u128{big_[1]} << 64);
  }
  void clear_unused() {
    const u32 rem = bits_ & 63;
    if (rem != 0) {
      data()[num_words() - 1] &= mask64(rem);
    }
  }
  [[nodiscard]] i64 sext64() const {
    const u32 sh = 64 - bits_;
    return static_cast<i64>(val_ << sh) >> sh;
  }

  // wide storage of the right size, content zeroed
  struct ZeroTag {};
  APInt(u32 bits, ZeroTag) : bits_(bits) {
    ASSERT(bits >= 1);
    if (is_single()) {
      val_ = 0;
    } else {
      big_ = static_cast<u64 *>(std::calloc(words_for(bits), sizeof(u64)));
    }
  }

  // out of line paths for everything wider than 64 bits
  [[nodiscard]] APInt zext_wide(u32 new_bits) const;
  [[nodiscard]] APInt sext_wide(u32 new_bits) const;
  [[nodiscard]] APInt trunc_wide(u32 new_bits) const;
  [[nodiscard]] APInt add_wide(const APInt &o) const;
  [[nodiscard]] APInt sub_wide(const APInt &o) const;
  [[nodiscard]] APInt mul_wide(const APInt &o) const;
  [[nodiscard]] APInt not_wide() const;
  [[nodiscard]] APInt and_wide(const APInt &o) const;
  [[nodiscard]] APInt or_wide(const APInt &o) const;
  [[nodiscard]] APInt xor_wide(const APInt &o) const;
  [[nodiscard]] APInt shl_wide(u32 amt) const;
  [[nodiscard]] APInt lshr_wide(u32 amt) const;
  [[nodiscard]] APInt ashr_wide(u32 amt) const;
  [[nodiscard]] APInt udiv_wide(const APInt &o) const;
  [[nodiscard]] APInt urem_wide(const APInt &o) const;
  [[nodiscard]] APInt sdiv_wide(const APInt &o) const;
  [[nodiscard]] APInt srem_wide(const APInt &o) const;
  [[nodiscard]] int ucmp_wide(const APInt &o) const;
  [[nodiscard]] int scmp_wide(const APInt &o) const;
  [[nodiscard]] bool eq_wide(const APInt &o) const;
  [[nodiscard]] bool is_zero_wide() const;
  static void udivrem_wide(const APInt &a, const APInt &b, APInt *q, APInt *r);

 public:
  APInt() : bits_(1), val_(0) {}
  // value is truncated to the width
  APInt(u32 bits, u64 value) : bits_(bits) {
    ASSERT(bits >= 1);
    if (bits <= 64) {
      val_ = value & mask64(bits);
    } else {
      big_ = static_cast<u64 *>(std::calloc(words_for(bits), sizeof(u64)));
      big_[0] = value;
    }
  }
  static APInt from_u64(u32 bits, u64 v) { return {bits, v}; }
  // sign extended from 64 bits to the width
  static APInt from_i64(u32 bits, i64 v);
  static APInt from_u128(u32 bits, u128 v);
  static APInt from_i128(u32 bits, i128 v);
  // little endian words, missing words are zero
  static APInt from_words(u32 bits, const u64 *words, u32 count);
  static APInt zero(u32 bits) { return {bits, u64{0}}; }
  static APInt all_ones(u32 bits);
  static APInt signed_max(u32 bits);
  static APInt signed_min(u32 bits);

  APInt(const APInt &o) : bits_(o.bits_) {
    if (is_single()) {
      val_ = o.val_;
    } else {
      const size_t n = words_for(bits_) * sizeof(u64);
      big_ = static_cast<u64 *>(std::malloc(n));
      std::memcpy(big_, o.big_, n);
    }
  }
  APInt(APInt &&o) noexcept : bits_(o.bits_) {
    if (is_single()) {
      val_ = o.val_;
    } else {
      big_ = o.big_;
      o.bits_ = 1;
      o.val_ = 0;
    }
  }
  APInt &operator=(const APInt &o) {
    if (this != &o) {
      APInt tmp(o);
      *this = static_cast<APInt &&>(tmp);
    }
    return *this;
  }
  APInt &operator=(APInt &&o) noexcept {
    if (this != &o) {
      if (!is_single()) {
        std::free(big_);
      }
      bits_ = o.bits_;
      if (is_single()) {
        val_ = o.val_;
      } else {
        big_ = o.big_;
        o.bits_ = 1;
        o.val_ = 0;
      }
    }
    return *this;
  }
  ~APInt() {
    if (!is_single()) {
      std::free(big_);
    }
  }

  [[nodiscard]] u32 bits() const { return bits_; }
  [[nodiscard]] u32 num_words() const { return words_for(bits_); }
  [[nodiscard]] const u64 *data() const { return is_single() ? &val_ : big_; }
  [[nodiscard]] u64 *data() { return is_single() ? &val_ : big_; }

  // value conversions, truncated or sign extended depending on the width
  [[nodiscard]] u64 to_u64() const { return is_single() ? val_ : big_[0]; }
  [[nodiscard]] i64 to_i64() const {
    return is_single() ? sext64() : static_cast<i64>(big_[0]);
  }
  [[nodiscard]] u128 to_u128() const;
  [[nodiscard]] i128 to_i128() const;

  [[nodiscard]] bool bit(u32 i) const {
    ASSERT(i < bits_);
    return ((data()[i / 64] >> (i % 64)) & 1) != 0;
  }
  void set_bit(u32 i, bool v = true) {
    ASSERT(i < bits_);
    u64 &w = data()[i / 64];
    const u64 m = u64{1} << (i % 64);
    w = v ? (w | m) : (w & ~m);
  }
  [[nodiscard]] bool is_neg() const { return bit(bits_ - 1); }
  [[nodiscard]] bool is_zero() const {
    return is_single() ? val_ == 0 : is_zero_wide();
  }
  [[nodiscard]] bool is_one() const;
  [[nodiscard]] bool is_all_ones() const { return popcount() == bits_; }
  [[nodiscard]] bool is_pow2() const { return popcount() == 1; }
  [[nodiscard]] u32 popcount() const;
  [[nodiscard]] u32 clz() const;
  [[nodiscard]] u32 ctz() const;
  // number of bits needed to hold the value as unsigned, 0 for zero
  [[nodiscard]] u32 active_bits() const { return bits_ - clz(); }
  [[nodiscard]] u32 log2() const { return active_bits() - 1; }
  [[nodiscard]] bool fits_u64() const { return active_bits() <= 64; }

  // arithmetic
  [[nodiscard]] APInt operator+(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      return {bits_, val_ + o.val_};
    }
    return add_wide(o);
  }
  [[nodiscard]] APInt operator-(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      return {bits_, val_ - o.val_};
    }
    return sub_wide(o);
  }
  [[nodiscard]] APInt operator*(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      return {bits_, val_ * o.val_};
    }
    return mul_wide(o);
  }
  [[nodiscard]] APInt operator-() const { return zero(bits_) - *this; }
  [[nodiscard]] APInt operator~() const {
    if (is_single()) {
      return {bits_, ~val_};
    }
    return not_wide();
  }
  [[nodiscard]] APInt operator&(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      return {bits_, val_ & o.val_};
    }
    return and_wide(o);
  }
  [[nodiscard]] APInt operator|(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      return {bits_, val_ | o.val_};
    }
    return or_wide(o);
  }
  [[nodiscard]] APInt operator^(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      return {bits_, val_ ^ o.val_};
    }
    return xor_wide(o);
  }
  // in place, single word values are updated without a temporary
  APInt &operator+=(const APInt &o) {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      val_ = (val_ + o.val_) & mask64(bits_);
      return *this;
    }
    return *this = add_wide(o);
  }
  APInt &operator-=(const APInt &o) {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      val_ = (val_ - o.val_) & mask64(bits_);
      return *this;
    }
    return *this = sub_wide(o);
  }
  APInt &operator*=(const APInt &o) {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      val_ = (val_ * o.val_) & mask64(bits_);
      return *this;
    }
    return *this = mul_wide(o);
  }
  APInt &operator&=(const APInt &o) {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      val_ &= o.val_;
      return *this;
    }
    return *this = and_wide(o);
  }
  APInt &operator|=(const APInt &o) {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      val_ |= o.val_;
      return *this;
    }
    return *this = or_wide(o);
  }
  APInt &operator^=(const APInt &o) {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      val_ ^= o.val_;
      return *this;
    }
    return *this = xor_wide(o);
  }

  [[nodiscard]] APInt shl(u32 amt) const {
    if (is_single()) {
      return {bits_, amt >= 64 ? 0 : val_ << amt};
    }
    return shl_wide(amt);
  }
  [[nodiscard]] APInt lshr(u32 amt) const {
    if (is_single()) {
      return {bits_, amt >= 64 ? 0 : val_ >> amt};
    }
    return lshr_wide(amt);
  }
  [[nodiscard]] APInt ashr(u32 amt) const {
    if (is_single()) {
      return {bits_, static_cast<u64>(sext64() >> (amt >= 64 ? 63 : amt))};
    }
    return ashr_wide(amt);
  }

  [[nodiscard]] APInt udiv(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    ASSERT(!o.is_zero());
    if (is_single()) {
      return {bits_, val_ / o.val_};
    }
    return udiv_wide(o);
  }
  [[nodiscard]] APInt urem(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    ASSERT(!o.is_zero());
    if (is_single()) {
      return {bits_, val_ % o.val_};
    }
    return urem_wide(o);
  }
  [[nodiscard]] APInt sdiv(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    ASSERT(!o.is_zero());
    if (is_single()) {
      const i64 a = sext64();
      const i64 b = o.sext64();
      // the only overflowing case wraps back to the minimum
      return {bits_,
              b == -1 ? u64{0} - static_cast<u64>(a) : static_cast<u64>(a / b)};
    }
    return sdiv_wide(o);
  }
  [[nodiscard]] APInt srem(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    ASSERT(!o.is_zero());
    if (is_single()) {
      const i64 a = sext64();
      const i64 b = o.sext64();
      return {bits_, b == -1 ? u64{0} : static_cast<u64>(a % b)};
    }
    return srem_wide(o);
  }

  // arithmetic with overflow flag, the result is the wrapped value
  [[nodiscard]] APInt uadd_ov(const APInt &o, bool &ov) const;
  [[nodiscard]] APInt usub_ov(const APInt &o, bool &ov) const;
  [[nodiscard]] APInt umul_ov(const APInt &o, bool &ov) const;
  [[nodiscard]] APInt sadd_ov(const APInt &o, bool &ov) const;
  [[nodiscard]] APInt ssub_ov(const APInt &o, bool &ov) const;
  [[nodiscard]] APInt smul_ov(const APInt &o, bool &ov) const;

  // width changes
  [[nodiscard]] APInt zext(u32 new_bits) const {
    ASSERT(new_bits >= bits_);
    if (new_bits <= 64) {
      return {new_bits, val_};
    }
    return zext_wide(new_bits);
  }
  [[nodiscard]] APInt sext(u32 new_bits) const {
    ASSERT(new_bits >= bits_);
    if (new_bits <= 64) {
      return {new_bits, static_cast<u64>(sext64())};
    }
    return sext_wide(new_bits);
  }
  [[nodiscard]] APInt trunc(u32 new_bits) const {
    ASSERT(new_bits >= 1 && new_bits <= bits_);
    if (new_bits <= 64) {
      return {new_bits, to_u64()};
    }
    return trunc_wide(new_bits);
  }
  [[nodiscard]] APInt zext_or_trunc(u32 new_bits) const {
    return new_bits > bits_ ? zext(new_bits) : trunc(new_bits);
  }
  [[nodiscard]] APInt sext_or_trunc(u32 new_bits) const {
    return new_bits > bits_ ? sext(new_bits) : trunc(new_bits);
  }

  // comparisons
  [[nodiscard]] bool operator==(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    return is_single() ? val_ == o.val_ : eq_wide(o);
  }
  [[nodiscard]] bool operator!=(const APInt &o) const { return !(*this == o); }
  // three way, -1 / 0 / 1
  [[nodiscard]] int ucmp(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      return static_cast<int>(val_ > o.val_) - static_cast<int>(val_ < o.val_);
    }
    return ucmp_wide(o);
  }
  [[nodiscard]] int scmp(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    if (is_single()) {
      const i64 a = sext64();
      const i64 b = o.sext64();
      return static_cast<int>(a > b) - static_cast<int>(a < b);
    }
    return scmp_wide(o);
  }
  [[nodiscard]] bool ult(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    return is_single() ? val_ < o.val_ : ucmp_wide(o) < 0;
  }
  [[nodiscard]] bool ule(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    return is_single() ? val_ <= o.val_ : ucmp_wide(o) <= 0;
  }
  [[nodiscard]] bool ugt(const APInt &o) const { return o.ult(*this); }
  [[nodiscard]] bool uge(const APInt &o) const { return o.ule(*this); }
  [[nodiscard]] bool slt(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    return is_single() ? sext64() < o.sext64() : scmp_wide(o) < 0;
  }
  [[nodiscard]] bool sle(const APInt &o) const {
    ASSERT(bits_ == o.bits_);
    return is_single() ? sext64() <= o.sext64() : scmp_wide(o) <= 0;
  }
  [[nodiscard]] bool sgt(const APInt &o) const { return o.slt(*this); }
  [[nodiscard]] bool sge(const APInt &o) const { return o.sle(*this); }

  [[nodiscard]] u64 hash() const;
  // radix 2, 10 or 16, signed values get a leading minus sign
  [[nodiscard]] std::string to_string(bool is_signed = false,
                                      u32 radix = 10) const;
};

}  // namespace foptim
