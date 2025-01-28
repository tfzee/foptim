// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

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
