// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0
// XFAIL: *

int a[10] = {};
extern int b[10] __attribute__((alias("a")));
int off;
int main(void) {
  b[off] = 1;
  a[off] = 2;
  if (b[off] != 2) {
    return 1;
  }
  return 0;
}
