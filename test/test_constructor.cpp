// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: init101
// CHECK: init102
// CHECK: Result:0




#include <stdio.h>

__attribute__((constructor)) void init101() { puts("init101"); }
__attribute__((constructor)) void init102() { puts("init102"); }

int main(){
  return 0;
}

