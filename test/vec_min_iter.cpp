// RUN: clang++ -O0 -fno-exceptions -fno-stack-protector %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:3
#include <vector>

int main() {
  std::vector<double> vec_data;

  double result = 3.0;
  {
    auto first = vec_data.rbegin();
    auto last = vec_data.rend();
    while (first != last)
      result = *first++;
  }
  return result;
}
