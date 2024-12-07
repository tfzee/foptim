// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#include <limits.h>

int n = 0;

void g(int i) { n++; }

void f(int m) {
  int i;
  i = m;
  do {
    g((int)((unsigned)i * INT_MAX) / 2);
  } while (--i > 0);
}

int main() {
  f(4);
  if (n != 4) {
    return 1;
  }
  return 0;
}
