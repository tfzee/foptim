// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:33


int global1 = 33;
typedef struct {
  int a;
  int *b;
  int c;
} A;


A global2 = {2, &global1, 3};

int main() { return *global2.b; }

