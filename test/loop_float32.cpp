#include <cstdio>
#include <vector>
// RUN: clang++ -fno-exceptions -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: 1.0000002.000000
// CHECK: Result:0

int main() {
  std::vector<float> vec;
  // vec.push_back((float)1);
  // vec.push_back((float)2);
  // vec.push_back((float)3);
  for (size_t i = 1; i < 3; ++i) {
    vec.push_back((float)i);
  }
  for (auto it = vec.begin(); it != vec.end(); ++it) {
    printf("%f", *it);
  }
  return 0;
}
