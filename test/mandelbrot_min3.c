// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:1

int main() {
  double zi = 0.0;
  int z = 0;

  while (z < 1) {
    zi = 0.0 * zi + -1.0;

    if (zi * zi > 0.0) {
      return 1;
    }
    z += 1;
  }

  return 0;
}
