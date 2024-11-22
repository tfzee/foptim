// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0
// XFAIL: *

int foo(unsigned int n) {
  int i, j = -1;

  for (i = 0; i < 10 && j < 0; i++) {
    if ((1UL << i) == n)
      j = i;
  }

  if (j < 0)
    return 1;
  return 0;
}

int main(void) { return foo(64); }
