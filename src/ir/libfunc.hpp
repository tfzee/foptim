#pragma once
#include <cmath>
#include <optional>
#include <string>
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
  Fmod,
  Sqrt,
  Strlen,
  Strcmp,
  Memcpy,
  Memset,
  Memmove,
  Memcmp,
  MemcpyChk,
  MemsetChk,
};

struct LibFuncInfo {
  LibFunc func;
  u8 n_args;
};

// name of the double version, float has an f suffix, long double an l (not for
// exact entries)
struct LibFuncName {
  std::string_view name;
  LibFunc func;
  u8 n_args;
  bool exact = false;
};

// folded on constant floating point arguments
inline bool is_math_libfunc(LibFunc f) {
  return f >= LibFunc::Log && f <= LibFunc::Fmod;
}

inline bool is_string_libfunc(LibFunc f) {
  return f == LibFunc::Strlen || f == LibFunc::Strcmp;
}

// the ones the backend also gets from the intrinsics of the same name
inline bool is_mem_libfunc(LibFunc f) {
  return f >= LibFunc::Memcpy && f <= LibFunc::Memcmp;
}

inline constexpr LibFuncName LibFuncNames[] = {
    {"log", LibFunc::Log, 1},
    {"log2", LibFunc::Log2, 1},
    {"log10", LibFunc::Log10, 1},
    {"exp", LibFunc::Exp, 1},
    {"exp2", LibFunc::Exp2, 1},
    {"sin", LibFunc::Sin, 1},
    {"cos", LibFunc::Cos, 1},
    {"tan", LibFunc::Tan, 1},
    {"pow", LibFunc::Pow, 2},
    {"fmod", LibFunc::Fmod, 2},
    {"sqrt", LibFunc::Sqrt, 1},
    {"strlen", LibFunc::Strlen, 1, true},
    {"strcmp", LibFunc::Strcmp, 2, true},
    {"memcpy", LibFunc::Memcpy, 3, true},
    {"memset", LibFunc::Memset, 3, true},
    {"memmove", LibFunc::Memmove, 3, true},
    {"memcmp", LibFunc::Memcmp, 3, true},
    {"__memcpy_chk", LibFunc::MemcpyChk, 4, true},
    {"__memset_chk", LibFunc::MemsetChk, 4, true},
};

inline std::optional<LibFuncName> lookup_libfunc(std::string_view name) {
  for (const auto &e : LibFuncNames) {
    if (name == e.name) {
      return e;
    }
    if (!e.exact && name.size() == e.name.size() + 1 &&
        name.starts_with(e.name) &&
        (name.back() == 'f' || name.back() == 'l')) {
      return e;
    }
  }
  return std::nullopt;
}

// libm function a scalar `llvm.<name>.f32` / `llvm.<name>.f64` math intrinsic
// stands for (`log` / `logf`), empty if it is none
inline std::string intrinsic_libm_name(std::string_view llvm_name) {
  if (!llvm_name.starts_with("llvm.")) {
    return {};
  }
  llvm_name.remove_prefix(5);
  const bool is_f32 = llvm_name.ends_with(".f32");
  if (!is_f32 && !llvm_name.ends_with(".f64")) {
    return {};
  }
  llvm_name.remove_suffix(4);
  // the intrinsics that clang lowers to libm calls (the rest has its own
  // lowering)
  for (const std::string_view known :
       {"log", "log2", "log10", "exp", "exp2", "sin", "cos", "tan", "pow"}) {
    if (llvm_name == known) {
      return is_f32 ? std::string(known) + "f" : std::string(known);
    }
  }
  return {};
}

// name of the plain (double / exact) version, a string literal so it is null
// terminated
inline std::string_view libfunc_name(LibFunc f) {
  for (const auto &e : LibFuncNames) {
    if (e.func == f) {
      return e.name;
    }
  }
  return {};
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
    case LibFunc::Fmod:
      r = std::fmod(a, b);
      break;
    default:
      return std::nullopt;
  }
  if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(r)) {
    return std::nullopt;
  }
  return r;
}

}  // namespace foptim::fir
