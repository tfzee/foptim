// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
#include <limits.h>

void abort(void);
void exit(int);

int a[2];

void f(int i) {
  for (; i < INT_MAX; i++) {
    a[i] = -2;
    if (&a[i] == &a[1])
      break;
  }
}

int main() {
  a[0] = a[1] = 0;
  f(0);
  if (a[0] != -2 || a[1] != -2)
    return 1;
  return 0;
}
