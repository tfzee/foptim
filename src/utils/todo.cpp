#include "todo.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace foptim {


[[noreturn]] void todo_impl(const char *text, const char *filename,
                                   u64 line) {
  printf("[TODO] @ %s:%lu : %s\n", filename, line, text);
  exit(33);
  // std::abort();
}

void ASSERT_HANDLE(bool cond, const char *filename,
                                       size_t lineNumber, const char *message) {
  if (!cond) {
    std::cout << filename << ":" << lineNumber << " Failed assert! " << message
              << "\n\n";
    exit(33);
    // std::abort();
  }
}

} // namespace foptim
