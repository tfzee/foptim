#pragma once
#include <fmt/core.h>

#include "types.hpp"

#define unlikely(x) __builtin_expect(!!(x), 0)

namespace foptim {

#define TODO(text) foptim::todo_impl(text, __FILE__, __LINE__)
#define IMPL(text) foptim::impl_impl(text, __FILE__, __LINE__)
#define UNREACH() foptim::todo_impl("REACHED UNREACH!", __FILE__, __LINE__)

#define ASSERT(cond) foptim::ASSERT_HANDLE(cond, __FILE__, __LINE__, "");
#define ASSERT_M(cond, message) \
  foptim::ASSERT_HANDLE(cond, __FILE__, __LINE__, message);
// void ASSERT_HANDLE(bool cond, const char *filename, const size_t lineNumber,
//                    const char *message);

[[noreturn, gnu::cold]] inline void todo_impl(const char *text,
                                              const char *filename, u64 line) {
  fmt::println("[TODO] @ {}:{} : {}\n", filename, line, text);
  fflush(stdout);
  std::abort();
}

[[
#ifdef IMPL_ABORT
    noreturn
#endif
    ,
    gnu::cold]]
inline void impl_impl(const char *text, const char *filename, u64 line) {
  fmt::println("[IMPL] @ {}:{} : {}\n", filename, line, text);
#ifdef IMPL_ABORT
  fflush(stdout);
  std::abort();
#endif
}

#ifdef ASSERT_ENABLED
inline void ASSERT_HANDLE(bool cond, const char *filename, size_t lineNumber,
                          const char *message) {
  if (unlikely(!cond)) {
    fmt::println("{}:{} Failed Assert! {}\n", filename, lineNumber, message);
    fflush(stdout);
    std::abort();
  }
}
#else
[[clang::always_inline]] constexpr void ASSERT_HANDLE(
    bool /*cond*/, const char * /*filename*/, size_t /*lineNumber*/,
    const char * /*message*/) {}
#endif

}  // namespace foptim
