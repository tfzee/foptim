// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0

int main(void) {
  int i, j, k[3];

  j = 0;
  for (i = 0; i < 3; i++) {
    k[i] = j++;
  }

  for (i = 2; i >= 0; i--) {
    if (k[i] != i) {
      return 1;
    }
  }

  return 0;
}
