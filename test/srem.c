// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

int test(int a, int b) {
  if (a % 5 != 2) {
    return 1;
  }
  if (a % 7 != 4) {
    return 2;
  }
  if (b % 5 != -2) {
    return 3;
  }
  if (b % 7 != -4) {
    return 4;
  }
  return 0;
}

int main() {
  int a = 32;
  int b = -32;

  if (a % 5 != 2) {
    return 1;
  }
  if (a % 7 != 4) {
    return 2;
  }
  if (b % 5 != -2) {
    return 3;
  }
  if (b % 7 != -4) {
    return 4;
  }
  return test(a, b);
}
