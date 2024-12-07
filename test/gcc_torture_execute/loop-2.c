// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
// XFAIL: *

int a[2];

void f(unsigned int b) {
  unsigned int i;
  for (i = 0; i < b; i++) {
    a[i] = i - (unsigned int)2;
  }
}

int main() {
  a[0] = a[1] = 0;
  f(2);
  if (a[0] != -2 || a[1] != -1) {
    return 1;
  }
  return 0;
}
