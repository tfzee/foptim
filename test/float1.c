// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#define EPS 0.00001f
float test(float a) { return a + 2.F; }

int main() {
  float x = 0.F;
  float res = test(x);
  if (res < 2 - EPS) {
    return 1;
  }
  if (res > 2 + EPS) {
    return 2;
  }
  return 0;
}
