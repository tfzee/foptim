// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: 255.500000 255 127.700000 127Result:0

#include <stdio.h>

double test1 = 255.5;
double test2 = 127.7;

int main() {
  printf("%f %zu ", test1, (size_t)test1);
  printf("%f %zu", test2, (size_t)test2);
  return 0;
}
