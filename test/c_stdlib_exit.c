// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out -ecrt0
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:35

void exit(int);

int main() {
  exit(35);
  return 0;
}
