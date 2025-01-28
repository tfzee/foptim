// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out "okak"); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:2


int main(int argc, char** argv) {
  (void)argv;
  return argc;
}

