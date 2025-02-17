// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#include <iostream>

#define N 2

int main() {
  float l[N][N];
  float u[N][N];

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      l[i][j] = 32;
    }
  }
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      std::cout << u[i][j];
    }
  }
  return 0;
}
