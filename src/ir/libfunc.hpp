#pragma once
#include <cmath>
#include <optional>
#include <string_view>

#include "utils/types.hpp"

namespace foptim::fir {

// C library functions the compiler knows, set by the loader
enum class LibFunc : u8 {
  None,
  Log,
  Log2,
  Log10,
  Exp,
  Exp2,
  Sin,
  Cos,
  Tan,
  Pow,
};

struct LibFuncInfo {
  LibFunc func;
  u8 n_args;
};

// name of the double version, float has an f suffix, long double an l
struct LibFuncName {
  std::string_view name;
  LibFunc func;
  u8 n_args;
};

inline constexpr LibFuncName LibFuncNames[] = {
    {"log", LibFunc::Log, 1},   {"log2", LibFunc::Log2, 1},
    {"log10", LibFunc::Log10, 1}, {"exp", LibFunc::Exp, 1},
    {"exp2", LibFunc::Exp2, 1}, {"sin", LibFunc::Sin, 1},
    {"cos", LibFunc::Cos, 1},   {"tan", LibFunc::Tan, 1},
    {"pow", LibFunc::Pow, 2},
};

inline std::optional<LibFuncName> lookup_libfunc(std::string_view name) {
  for (const auto &e : LibFuncNames) {
    if (name == e.name) {
      return e;
    }
    if (name.size() == e.name.size() + 1 && name.starts_with(e.name) &&
        (name.back() == 'f' || name.back() == 'l')) {
      return e;
    }
  }
  return std::nullopt;
}

// nullopt when the result is not finite, those calls set errno
template <class T>
inline std::optional<T> fold_libfunc(LibFunc f, T a, T b = T{}) {
  T r;
  switch (f) {
    case LibFunc::Log:
      r = std::log(a);
      break;
    case LibFunc::Log2:
      r = std::log2(a);
      break;
    case LibFunc::Log10:
      r = std::log10(a);
      break;
    case LibFunc::Exp:
      r = std::exp(a);
      break;
    case LibFunc::Exp2:
      r = std::exp2(a);
      break;
    case LibFunc::Sin:
      r = std::sin(a);
      break;
    case LibFunc::Cos:
      r = std::cos(a);
      break;
    case LibFunc::Tan:
      r = std::tan(a);
      break;
    case LibFunc::Pow:
      r = std::pow(a, b);
      break;
    case LibFunc::None:
      return std::nullopt;
  }
  if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(r)) {
    return std::nullopt;
  }
  return r;
}

}  // namespace foptim::fir
