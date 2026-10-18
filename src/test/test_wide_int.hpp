#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <random>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/interpreter/interpreter.hpp"
#include "ir/value.hpp"
#include "utils/APInt.hpp"

namespace {
using foptim::APInt;
using foptim::fir::ICmpInstrSubType;
using foptim::fir::ValueR;
using wu128 = unsigned __int128;
using wi128 = __int128;

wu128 wide_mask(unsigned w) { return w == 128 ? ~wu128{0} : ((wu128{1} << w) - 1); }
wi128 wsext(wu128 x, unsigned w) {
  const unsigned sh = 128 - w;
  return static_cast<wi128>(x << sh) >> sh;
}

wu128 wrnd(std::mt19937_64 &g) {
  switch (g() % 5) {
    case 0:
      return g() % 8;
    case 1:
      return static_cast<wu128>(g());
    case 2:
      return ~wu128{0} - (g() % 4);
    case 3:
      return wu128{1} << (g() % 128);
    default:
      return (static_cast<wu128>(g()) << 64) | g();
  }
}

// f(a, b) = op(a, b) on iW values, interpreted with the FIR interpreter
struct WideFn {
  foptim::fir::Context ctx{nullptr};
  foptim::fir::TypeR ty;
  foptim::fir::TypeR res_ty;
  foptim::fir::FunctionR f;
  unsigned w;

  // context of the function being built, for ops that need more types
  static inline foptim::fir::Context *current = nullptr;
  using Op = std::function<ValueR(foptim::fir::Builder &, ValueR, ValueR)>;

  WideFn(unsigned width, const Op &op, unsigned res_width = 0)
      : ty(ctx.data->get_int_type(width)),
        res_ty(ctx.data->get_int_type(res_width ? res_width : width)),
        f(ctx.data->create_function(
            "f", ctx.data->get_func_ty(res_ty, {ty, ty}))),
        w(width) {
    auto b = f.builder();
    auto entry = f.func->get_entry();
    b.at_end(entry);
    current = &ctx;
    b.build_return(op(b, ValueR{entry->args[0]}, ValueR{entry->args[1]}));
  }

  // nullopt if the interpreter refused (poison, division by zero)
  std::optional<APInt> run(wu128 a, wu128 b) {
    foptim::fir::intepreter::Interpreter interp{f.func};
    auto entry = f.func->get_entry();
    interp.set(ValueR{entry->args[0]},
               foptim::fir::ConstantValue{APInt::from_u128(w, a), ty});
    interp.set(ValueR{entry->args[1]},
               foptim::fir::ConstantValue{APInt::from_u128(w, b), ty});
    if (!interp.run()) {
      return std::nullopt;
    }
    return interp.return_values()[0].as_apint();
  }
};

constexpr unsigned kWidths[] = {65, 96, 127, 128};

template <class Ref>
void check_binary(const char *name, const WideFn::Op &op, Ref ref) {
  for (unsigned w : kWidths) {
    WideFn fn{w, op};
    std::mt19937_64 g(w);
    for (int i = 0; i < 300; i++) {
      const wu128 a = wrnd(g) & wide_mask(w);
      const wu128 b = wrnd(g) & wide_mask(w);
      auto got = fn.run(a, b);
      auto want = ref(a, b, w);
      ASSERT_EQ(got.has_value(), want.has_value())
          << name << " w=" << w << " i=" << i;
      if (want) {
        ASSERT_TRUE(*got == APInt::from_u128(w, *want & wide_mask(w)))
            << name << " w=" << w << " a=" << (u64)a << " b=" << (u64)b;
      }
    }
  }
}

using R = std::optional<wu128>;
}  // namespace

TEST(WideInt, AddSubMulBitwise) {
  check_binary(
      "add", [](auto &b, ValueR x, ValueR y) { return b.build_int_add(x, y); },
      [](wu128 a, wu128 b, unsigned) -> R { return a + b; });
  check_binary(
      "sub", [](auto &b, ValueR x, ValueR y) { return b.build_int_sub(x, y); },
      [](wu128 a, wu128 b, unsigned) -> R { return a - b; });
  check_binary(
      "mul", [](auto &b, ValueR x, ValueR y) { return b.build_int_mul(x, y); },
      [](wu128 a, wu128 b, unsigned) -> R { return a * b; });
  check_binary(
      "and", [](auto &b, ValueR x, ValueR y) { return b.build_binary_op(x, y, foptim::fir::BinaryInstrSubType::And); },
      [](wu128 a, wu128 b, unsigned) -> R { return a & b; });
  check_binary(
      "or", [](auto &b, ValueR x, ValueR y) { return b.build_binary_op(x, y, foptim::fir::BinaryInstrSubType::Or); },
      [](wu128 a, wu128 b, unsigned) -> R { return a | b; });
  check_binary(
      "xor", [](auto &b, ValueR x, ValueR y) { return b.build_binary_op(x, y, foptim::fir::BinaryInstrSubType::Xor); },
      [](wu128 a, wu128 b, unsigned) -> R { return a ^ b; });
}

TEST(WideInt, Shifts) {
  // out of range amounts are poison and refused by the interpreter
  check_binary(
      "shl", [](auto &b, ValueR x, ValueR y) { return b.build_shl(x, y); },
      [](wu128 a, wu128 b, unsigned w) -> R {
        if (b >= w) return std::nullopt;
        return a << static_cast<unsigned>(b);
      });
  check_binary(
      "lshr", [](auto &b, ValueR x, ValueR y) { return b.build_lshr(x, y); },
      [](wu128 a, wu128 b, unsigned w) -> R {
        if (b >= w) return std::nullopt;
        return a >> static_cast<unsigned>(b);
      });
  check_binary(
      "ashr", [](auto &b, ValueR x, ValueR y) { return b.build_ashr(x, y); },
      [](wu128 a, wu128 b, unsigned w) -> R {
        if (b >= w) return std::nullopt;
        return static_cast<wu128>(wsext(a, w) >> static_cast<unsigned>(b));
      });
}

TEST(WideInt, DivRem) {
  check_binary(
      "udiv",
      [](auto &b, ValueR x, ValueR y) {
        return b.build_binary_op(x, y, foptim::fir::BinaryInstrSubType::IntUDiv);
      },
      [](wu128 a, wu128 b, unsigned) -> R {
        if (b == 0) return std::nullopt;
        return a / b;
      });
  check_binary(
      "sdiv",
      [](auto &b, ValueR x, ValueR y) { return b.build_binary_op(x, y, foptim::fir::BinaryInstrSubType::IntSDiv); },
      [](wu128 a, wu128 b, unsigned w) -> R {
        const wi128 sa = wsext(a, w);
        const wi128 sb = wsext(b, w);
        if (sb == 0) return std::nullopt;
        if (sb == -1 && sa == wsext(wu128{1} << (w - 1), w)) return std::nullopt;
        return static_cast<wu128>(sa / sb);
      });
  check_binary(
      "urem",
      [](auto &b, ValueR x, ValueR y) { return b.build_int_urem(x, y); },
      [](wu128 a, wu128 b, unsigned) -> R {
        if (b == 0) return std::nullopt;
        return a % b;
      });
  check_binary(
      "srem",
      [](auto &b, ValueR x, ValueR y) { return b.build_int_srem(x, y); },
      [](wu128 a, wu128 b, unsigned w) -> R {
        const wi128 sa = wsext(a, w);
        const wi128 sb = wsext(b, w);
        if (sb == 0) return std::nullopt;
        if (sb == -1 && sa == wsext(wu128{1} << (w - 1), w)) return std::nullopt;
        return static_cast<wu128>(sa % sb);
      });
}

TEST(WideInt, Compare) {
  struct P {
    ICmpInstrSubType p;
    std::function<bool(wu128, wu128, unsigned)> ref;
  };
  const P preds[] = {
      {ICmpInstrSubType::EQ, [](wu128 a, wu128 b, unsigned) { return a == b; }},
      {ICmpInstrSubType::NE, [](wu128 a, wu128 b, unsigned) { return a != b; }},
      {ICmpInstrSubType::ULT, [](wu128 a, wu128 b, unsigned) { return a < b; }},
      {ICmpInstrSubType::UGE, [](wu128 a, wu128 b, unsigned) { return a >= b; }},
      {ICmpInstrSubType::SLT,
       [](wu128 a, wu128 b, unsigned w) { return wsext(a, w) < wsext(b, w); }},
      {ICmpInstrSubType::SGE,
       [](wu128 a, wu128 b, unsigned w) { return wsext(a, w) >= wsext(b, w); }},
  };
  for (const auto &p : preds) {
    for (unsigned w : kWidths) {
      WideFn fn{w,
                [&](auto &b, ValueR x, ValueR y) {
                  return b.build_int_cmp(x, y, p.p);
                },
                1};
      std::mt19937_64 g(w);
      for (int i = 0; i < 200; i++) {
        const wu128 a = wrnd(g) & wide_mask(w);
        // equal operands are the interesting case for eq / ge
        const wu128 b = i % 5 == 0 ? a : wrnd(g) & wide_mask(w);
        auto got = fn.run(a, b);
        ASSERT_TRUE(got.has_value());
        ASSERT_EQ(got->to_u64(), p.ref(a, b, w) ? 1u : 0u) << "w=" << w;
      }
    }
  }
}

TEST(WideInt, Casts) {
  // zext / sext from i64 into the wide type and trunc back
  for (unsigned w : kWidths) {
    auto ext = [&](bool sign) {
      return std::make_unique<WideFn>(w, [&](auto &b, ValueR x, ValueR) {
        auto *c = WideFn::current->data;
        auto t = b.build_itrunc(x, c->get_int_type(64));
        return sign ? b.build_sext(t, c->get_int_type(w))
                    : b.build_zext(t, c->get_int_type(w));
      });
    };
    std::mt19937_64 g(w);
    auto z = ext(false);
    auto s = ext(true);
    for (int i = 0; i < 200; i++) {
      const wu128 a = wrnd(g) & wide_mask(w);
      const u64 lo = static_cast<u64>(a);
      ASSERT_TRUE(*z->run(a, 0) == APInt::from_u128(w, lo));
      ASSERT_TRUE(*s->run(a, 0) ==
                  APInt::from_u128(w, static_cast<wu128>(static_cast<foptim::i64>(lo)) & wide_mask(w)));
    }
  }
}

TEST(WideInt, Above128) {
  // i256: (a + b) - b == a, (a << 130) >> 130 keeps the low 126 bits
  WideFn fn{256, [](auto &b, ValueR x, ValueR y) {
              return b.build_int_sub(b.build_int_add(x, y), y);
            }};
  auto entry = fn.f.func->get_entry();
  foptim::fir::intepreter::Interpreter interp{fn.f.func};
  const u64 words_a[4] = {1, ~u64{0}, 0x8000000000000000ULL, 7};
  const u64 words_b[4] = {~u64{0}, ~u64{0}, 3, 0xdeadbeef};
  auto a = APInt::from_words(256, words_a, 4);
  auto b = APInt::from_words(256, words_b, 4);
  interp.set(ValueR{entry->args[0]}, foptim::fir::ConstantValue{a, fn.ty});
  interp.set(ValueR{entry->args[1]}, foptim::fir::ConstantValue{b, fn.ty});
  ASSERT_TRUE(interp.run());
  EXPECT_TRUE(interp.return_values()[0].as_apint() == a);
}
