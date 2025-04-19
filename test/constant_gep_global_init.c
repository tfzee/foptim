// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:100

int data[100];

int *iterator_start = data;
int *iterator_end = data + 100;

int main() { return iterator_end - iterator_start; }

