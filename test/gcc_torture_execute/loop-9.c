// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

int fals() { return 0; }

int main() {
  int count = 0;

  while (fals() || count < -123) {
    ++count;
  }

  if (count) {
    return 1;
  }

  return 0;
}
