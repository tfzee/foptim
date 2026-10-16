#pragma once
#include <bit>
#include <optional>

#include "utils/types.hpp"

namespace foptim::fmir {

// Division by a constant as multiply and shift (Granlund and Montgomery).
// All values are for a division of `width` bit integers (width <= 64).

// q = ((x >> pre_shift) * mul) >> shift                      if !add
// t = (x * mul) >> width
// q = ((t + ((x - t) >> 1)) >> (shift - 1))                  if add
// The products are full (double width) products. For !add and width == 64 the
// shift is at least 64, so it can be done as a high multiply.
struct UDivMagic {
  u64 mul;
  u8 pre_shift;
  u8 shift;
  bool add;
};

// d must be in [3, 2^(width-1)] and not a power of two, other divisors have no
// entry (the caller keeps the division instruction).
inline std::optional<UDivMagic> udiv_magic(u64 d, unsigned width) {
  using u128 = unsigned __int128;
  if (width > 64 || width < 2 || d < 3 || std::has_single_bit(d) ||
      (width < 64 && d >= (u64{1} << (width - 1))) ||
      (width == 64 && d > (u64{1} << 63))) {
    return std::nullopt;
  }
  const u128 max_mul = (width == 64) ? ~u64{0} : ((u128{1} << width) - 1);
  const unsigned tz = std::countr_zero(d);
  for (unsigned try_pre = 0; try_pre < 2; try_pre++) {
    const unsigned pre = try_pre == 0 ? 0 : tz;
    if (try_pre == 1 && tz == 0) {
      break;
    }
    const u64 dd = d >> pre;
    const unsigned wp = width - pre;
    const unsigned l = std::bit_width(dd - 1);
    // the shift after a high multiply must not be negative
    const unsigned first = (width == 64) ? pre : 0;
    for (unsigned s = first; s <= l; s++) {
      const u128 num = u128{1} << (wp + s);
      const u128 m = (num + dd - 1) / dd;
      const u128 e = m * dd - num;
      if (e <= (u128{1} << s) && m <= max_mul) {
        return UDivMagic{.mul = static_cast<u64>(m),
                         .pre_shift = static_cast<u8>(pre),
                         .shift = static_cast<u8>(wp + s),
                         .add = false};
      }
    }
  }
  const unsigned l = std::bit_width(d - 1);
  const u128 m = (u128{1} << (width + l)) / d + 1;
  return UDivMagic{.mul = static_cast<u64>(m - (u128{1} << width)),
                   .pre_shift = 0,
                   .shift = static_cast<u8>(l),
                   .add = true};
}

// For |d|: q0 = (sext(x) * mul) >> shift (arithmetic) and the quotient of
// |d| is q0 + (q0 >> (width - 1) logical, i.e. 1 if q0 is negative).
// Negative divisors negate the result. For width == 64 the product is the
// double width one and shift is at least 64. mul is below 2^width and is used
// as an unsigned number, so for width == 64 it can be above INT64_MAX (then
// the high multiply needs the x correction).
struct SDivMagic {
  u64 mul;
  u8 shift;
};

// d must be |d| >= 3 and not a power of two
inline std::optional<SDivMagic> sdiv_magic(u64 d, unsigned width) {
  using u128 = unsigned __int128;
  if (width > 64 || width < 3 || d < 3 || std::has_single_bit(d) ||
      d > (u64{1} << (width - 1))) {
    return std::nullopt;
  }
  const u128 max_mul = (width == 64) ? ~u64{0} : ((u128{1} << width) - 1);
  const unsigned l = std::bit_width(d - 1);
  const unsigned first = (width == 64) ? 1 : 0;
  for (unsigned s = first; s <= l; s++) {
    const u128 num = u128{1} << (width - 1 + s);
    const u128 m = (num + d - 1) / d;
    const u128 e = m * d - num;
    if (e <= (u128{1} << s) && m <= max_mul) {
      return SDivMagic{.mul = static_cast<u64>(m),
                       .shift = static_cast<u8>(width - 1 + s)};
    }
  }
  return std::nullopt;
}

} // namespace foptim::fmir
