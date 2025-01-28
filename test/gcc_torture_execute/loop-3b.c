// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#include <limits.h>

int n = 0;

void g(int) { n++; }

void f(int m) {
  int i;
  i = m;
  do {
    g(i * 4);
    i -= INT_MAX / 8;
  } while (i > 0);
}

int main() {
  f(INT_MAX / 8 * 4);
  if (n != 4) {
    return 1;
  }
  return 0;
}
