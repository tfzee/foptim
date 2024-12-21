// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out -ecrt0
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

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
