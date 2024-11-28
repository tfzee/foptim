// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0

unsigned fib(unsigned n) {
  if (n <= 1) {
    return n;
  }

  return fib(n - 1) + fib(n - 2);
}

int main() {
  // Sample input
  unsigned n = 35;
  // Printing the nth fibonacci Number
  return (fib(n) == 9227465) ? 0 : 1;
}
