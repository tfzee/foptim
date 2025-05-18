// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: test 0 failed Result:0
#include <cstdio>
int current_test = 0;

int main() {
  printf("test %i failed\n", current_test);
  return 0;
}
