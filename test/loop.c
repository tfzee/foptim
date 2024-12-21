// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out -ecrt0
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:15


int main() {
  int a = 5;
  int b = 0;
  int c = 2;

  while (a < 10) {
    b += 1 + c;
    a = a + 1;
  }
  return b;
}
