// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0
// XFAIL: *

int val;

int *ptr = &val;
float *ptr2 = (float *)&val;

void typepun() { *ptr2 = 0; }

int main(void) {
  *ptr = 1;
  typepun();
  if (*ptr) {
    return 1;
  }

  return 0;
}
