// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: test 0 failed Result:0
#include <cmath>
#include <cstdio>
#include <ctime>
int current_test = 0;

inline void check_shifted_sum() { printf("test %i failed\n", current_test); };

template <typename T> void test_constant() {
  for (int i = 0; i < 10; ++i) {
    check_shifted_sum();
  }
};

int main() { test_constant<double>(); }
