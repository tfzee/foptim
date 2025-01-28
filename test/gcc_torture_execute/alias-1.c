// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
// X FAIL: *

int val;

int *ptr = 0;
float *ptr2 = 0;

void typepun() { *ptr2 = 0; }

int main(void) {
  ptr = &val;
  ptr2 = (float *)&val;

  *ptr = 1;
  typepun();
  if (*ptr) {
    return 1;
  }

  return 0;
}
