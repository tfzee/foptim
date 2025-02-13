#include "types.hpp"
#include <bit>
#include <cstring>
#include <limits>

namespace foptim::utils {

struct Int128 {
  i64 high;
  u64 low;

  consteval Int128() : high(0), low(0) {}
  constexpr Int128(i64 h, u64 l) : high(h), low(l) {}
  constexpr Int128(i64 a) : high(0), low(0) {
    if (a < 0) {
      high = -1;
    }
    low = std::bit_cast<u64>(a);
  }

  consteval static Int128 max() {
    Int128 r;
    r.high = std::numeric_limits<i64>::max();
    r.low = std::numeric_limits<u64>::max();
    return r;
  }

  consteval static Int128 min() {
    Int128 r;
    r.low = std::numeric_limits<u64>::max();
    r.high = std::numeric_limits<i64>::min();
    return r;
  }

  constexpr Int128 &operator-=(const Int128 &b) {
    bool carry = __builtin_usubl_overflow(low, b.low, &low);
    high = high - b.high - carry;
    return *this;
  }

  constexpr Int128 &operator+=(const Int128 &b) {
    bool carry = __builtin_uaddl_overflow(low, b.low, &low);
    high = high + b.high + carry;
    return *this;
  }

  constexpr Int128 operator+(const Int128 &b) const {
    Int128 res = *this;
    res += b;
    return res;
  }

  constexpr Int128 operator-(const Int128 &b) const {
    Int128 res = *this;
    res -= b;
    return res;
  }

  constexpr bool operator==(const Int128 &b) const {
    return high == b.high && low == b.low;
  }

  constexpr bool operator<(const Int128 &b) const { return high < b.high || low < b.low; }
  constexpr bool operator>(const Int128 &b) const { return high > b.high || low > b.low; }
};

} // namespace foptim::utils
