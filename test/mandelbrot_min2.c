// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

int main() {
  int sum = 0;
  int z = 0;
  double zi = 0.0;
  while (z < 50) {
    zi = 0.0 * zi;
    if (0.0 + zi > 0.0) {
      sum = 1;
    }
    z += 1;
  }
  return sum;
}
