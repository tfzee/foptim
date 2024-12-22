// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

int f() {
  int j = 1;
  long i;
  i = 0x60000000L;
  do {
    j <<= 1;
    i += 0x10000000L;
  } while (i < -0x60000000L);
  return j;
}

int main() {
  if (f() != 2) {
    return 1;
  }
  return 0;
}
