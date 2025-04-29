// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: 0Result:0
#include <stdio.h>

#define N 16

int a[N] = {-1, -1, -1};


int main() {
  int testi = (a[0] > 4 ? 4 : 0);
  printf("%i", testi);
  return 0;
}
