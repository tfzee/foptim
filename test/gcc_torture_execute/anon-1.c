// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0

struct {
  int x;
  struct {
    int a;
    union {
      int b;
    };
  };
} foo;

int main() {
  foo.b = 6;
  foo.a = 5;

  if (foo.b != 6) {
    return 1;
  }

  return 0;
}
