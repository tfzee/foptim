#include <bit>

#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "matcher.hpp"
#include "mir/const_div.hpp"
#include "mir/instr.hpp"
#include "mir/matcher_helpers.hpp"
#include "mir/reg_alloc.hpp"

namespace foptim::fmir {

namespace {
// Emits the sequence for one division by a constant. Everything is computed
// in 64 bit registers (the operand is extended first), only the result is
// truncated again. Might be bad idea but lazy
struct ConstDivEmitter {
  TVec<MInstr> &out;
  DumbRegAlloc &alloc;

  MArgument fresh() {
    return MArgument(alloc.get_new_register(Type::Int64), Type::Int64);
  }
  MArgument copy(MArgument src) {
    auto dst = fresh();
    out.emplace_back(GBaseSubtype::mov, dst, src);
    return dst;
  }
  static MArgument imm8(unsigned v) { return MArgument(static_cast<u8>(v)); }
  MArgument shr(MArgument src, unsigned n) {
    auto dst = copy(src);
    out.emplace_back(GArithSubtype::shr2, dst, imm8(n));
    return dst;
  }
  MArgument sar(MArgument src, unsigned n) {
    auto dst = copy(src);
    out.emplace_back(GArithSubtype::sar2, dst, imm8(n));
    return dst;
  }
  MArgument shl(MArgument src, unsigned n) {
    auto dst = copy(src);
    out.emplace_back(GArithSubtype::shl2, dst, imm8(n));
    return dst;
  }
  MArgument add(MArgument a, MArgument b) {
    auto dst = copy(a);
    out.emplace_back(GArithSubtype::add2, dst, b);
    return dst;
  }
  MArgument sub(MArgument a, MArgument b) {
    auto dst = copy(a);
    out.emplace_back(GArithSubtype::sub2, dst, b);
    return dst;
  }
  MArgument neg(MArgument a) {
    auto dst = copy(a);
    out.emplace_back(GArithSubtype::neg1, dst);
    return dst;
  }
  MArgument mul_const(MArgument a, u64 c) {
    auto dst = copy(a);
    out.emplace_back(GArithSubtype::mul2, dst, MArgument(c));
    return dst;
  }
  MArgument const_reg(u64 c) {
    auto dst = fresh();
    out.emplace_back(GBaseSubtype::mov, dst, MArgument(c));
    return dst;
  }
  MArgument mulh(MArgument x, u64 c, bool is_signed) {
    auto rax = MArgument(VReg::RAX(), Type::Int64);
    auto rdx = MArgument(VReg::RDX(), Type::Int64);
    auto creg = const_reg(c);
    out.emplace_back(GBaseSubtype::mov, rax, x);
    out.emplace_back(is_signed ? GArithSubtype::smulh : GArithSubtype::umulh,
                     rax, rdx, rax, creg);
    return copy(rdx);
  }
  MArgument udiv(MArgument x, u64 d, unsigned width, bool &ok) {
    if (std::has_single_bit(d)) {
      return d == 1 ? x : shr(x, std::countr_zero(d));
    }
    auto magic = udiv_magic(d, width);
    if (!magic) {
      ok = false;
      return x;
    }
    if (!magic->add) {
      auto y = magic->pre_shift != 0 ? shr(x, magic->pre_shift) : x;
      if (width == 64) {
        return shr(mulh(y, magic->mul, false), magic->shift - 64);
      }
      return shr(mul_const(y, magic->mul), magic->shift);
    }
    MArgument t = width == 64 ? mulh(x, magic->mul, false)
                              : shr(mul_const(x, magic->mul), width);
    auto u = shr(sub(x, t), 1);
    return shr(add(u, t), magic->shift - 1);
  }

  // x is the sign extended operand, d the signed divisor
  MArgument sdiv(MArgument x, i64 d, unsigned width, bool &ok) {
    const u64 ad = d < 0 ? 0 - static_cast<u64>(d) : static_cast<u64>(d);
    MArgument q = x;
    if (ad == 1) {
      q = x;
    } else if (std::has_single_bit(ad)) {
      const unsigned k = std::countr_zero(ad);
      // add 2^k - 1 to negative values so the shift rounds towards zero
      auto bias = shr(sar(x, 63), 64 - k);
      q = sar(add(x, bias), k);
    } else {
      auto magic = sdiv_magic(ad, width);
      if (!magic) {
        ok = false;
        return x;
      }
      MArgument q0 = x;
      if (width == 64) {
        auto t = mulh(x, magic->mul, true);
        if (magic->mul > static_cast<u64>(std::numeric_limits<i64>::max())) {
          t = add(t, x);
        }
        q0 = sar(t, magic->shift - 64);
      } else {
        q0 = sar(mul_const(x, magic->mul), magic->shift);
      }
      q = add(q0, shr(q0, 63));
    }
    return d < 0 ? neg(q) : q;
  }
};
} // namespace

bool generate_const_div(fir::Instr instr, bool is_signed, bool is_rem,
                        MatchResult &res, ExtraMatchData &data) {
  if (!instr->args[1].is_constant() ||
      !instr->args[1].as_constant()->is_int()) {
    return false;
  }
  auto ty = convert_type(instr.result_type());
  if (ty != Type::Int8 && ty != Type::Int16 && ty != Type::Int32 &&
      ty != Type::Int64) {
    return false;
  }
  const unsigned width = get_size(ty) * 8;
  const u64 mask = width == 64 ? ~u64{0} : ((u64{1} << width) - 1);
  const u64 raw =
      static_cast<u64>(instr->args[1].as_constant()->as_int()) & mask;
  const i64 sd = static_cast<i64>(raw << (64 - width)) >> (64 - width);
  if (raw == 0 || (is_signed && sd == -1)) {
    // trap or overflow, the division instruction does what it does
    return false;
  }

  const u64 abs_d = sd < 0 ? 0 - static_cast<u64>(sd) : static_cast<u64>(sd);
  if (is_signed ? (!std::has_single_bit(abs_d) && !sdiv_magic(abs_d, width))
                : (!std::has_single_bit(raw) && !udiv_magic(raw, width))) {
    return false;
  }

  auto x_in = valueToArg(instr->args[0], res.result, data.alloc);
  if (!x_in.isReg()) {
    return false;
  }
  auto res_reg = valueToArg(instr.result(), res.result, data.alloc);

  ConstDivEmitter e{.out = res.result, .alloc = data.alloc};
  MArgument x = x_in;
  if (width < 64) {
    x = e.fresh();
    res.result.emplace_back(
        is_signed ? GConvSubtype::mov_sx : GConvSubtype::mov_zx, x, x_in);
  }
  bool ok = true;
  MArgument r = x;
  if (is_rem && !is_signed && std::has_single_bit(raw)) {
    r = e.copy(x);
    res.result.emplace_back(GArithSubtype::land2, r, MArgument(raw - 1));
  } else if (is_rem && is_signed && abs_d != 1 && std::has_single_bit(abs_d)) {
    const unsigned k = std::countr_zero(abs_d);
    auto bias = e.shr(e.sar(x, 63), 64 - k);
    r = e.sub(x, e.shl(e.sar(e.add(x, bias), k), k));
  } else {
    MArgument q =
        is_signed ? e.sdiv(x, sd, width, ok) : e.udiv(x, raw, width, ok);
    ASSERT(ok);
    if (is_rem) {
      r = e.sub(x, e.mul_const(q, is_signed ? static_cast<u64>(sd) : raw));
    } else {
      r = q;
    }
  }
  if (width < 64) {
    res.result.emplace_back(GConvSubtype::itrunc, res_reg, r);
  } else {
    res.result.emplace_back(GBaseSubtype::mov, res_reg, r);
  }
  return true;
}

} // namespace foptim::fmir
