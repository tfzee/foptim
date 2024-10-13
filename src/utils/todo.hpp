#pragma once
#include "types.hpp"

namespace foptim {

#define TODO(text) foptim::todo_impl(text, __FILE__, __LINE__)

#define ASSERT(cond) foptim::ASSERT_HANDLE(cond, __FILE__, __LINE__, "");
#define ASSERT_M(cond, message)                                                \
  foptim::ASSERT_HANDLE(cond, __FILE__, __LINE__, message);
// void ASSERT_HANDLE(bool cond, const char *filename, const size_t lineNumber,
//                    const char *message);

[[noreturn]] void todo_impl(const char *text, const char *filename,
                                   u64 line);

void ASSERT_HANDLE(bool cond, const char *filename, size_t lineNumber,
                          const char *message);

} // namespace foptim
