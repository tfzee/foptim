// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0

#define EPS 0.00001f
float test(float a) { return a + 2; }

int main() {
  float x = 0.0F;
  float res = test(x);
  if (res < 2 - EPS && res > 2 + EPS) {
    return 1;
  }
  return 0;
}
