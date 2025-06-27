// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:10

#include <cstdlib>

struct Test {
  float a;
  float b;
  float c;
  float d;
};

Test testfun(Test d) {
  return {.a = std::abs(d.a),
          .b = std::abs(d.b),
          .c = std::abs(d.c),
          .d = std::abs(d.d)};
}

int main() {
  Test d{.a = -1, .b = -2, .c = 3, .d = 4};
  auto test = testfun(d);
  return (int)(test.a + test.b + test.c + test.d);
}
