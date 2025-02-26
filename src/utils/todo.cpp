#include "todo.hpp"
#include <cstdio>
#include <cstdlib>
#include <fmt/core.h>

namespace foptim {

[[noreturn]] void todo_impl(const char *text, const char *filename, u64 line) {
  printf("[TODO] @ %s:%lu : %s\n", filename, line, text);
  exit(33);
  // std::abort();
}

[[noreturn]] void impl_impl(const char *text, const char *filename, u64 line) {
  printf("[IMPLEMENT] @ %s:%lu : %s\n", filename, line, text);
  exit(33);
  // std::abort();
}

void ASSERT_HANDLE(bool cond, const char *filename, size_t lineNumber,
                   const char *message) {
  if (!cond) {
    fmt::println("{}:{} Failed Assert! {}\n", filename, lineNumber, message);
    std::abort();
  }
}

} // namespace foptim
