// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
// XFAIL: *

int a = 1;
extern int b __attribute__((alias("a")));
int c = 1;
extern int d __attribute__((alias("c")));
int main() {
  int argc = 1;
  int *p;
  int *q;
  if (argc) {
    p = &a, q = &b;
  } else {
    p = &c, q = &d;
  }
  *p = 1;
  *q = 2;
  if (*p == 1) {
    return 1;
  }
  return 0;
}
