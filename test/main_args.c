// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out "okak" || echo Result:$? | FileCheck %s

// CHECK: Result:2


int main(int argc, char** argv) {
  (void)argv;
  return argc;
}

