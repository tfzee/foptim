// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:1

#define SIZE 32

extern "C" int main() {
  int a[SIZE * SIZE] = {};
  for (int i = 0; i < SIZE; i++) {
    for (int j = 0; j < SIZE; j++) {
      a[i * SIZE + j] = i;
    }
  }
  int sum = 0;
  for (int i : a) {
    sum += i;
  }
  return static_cast<int>(sum == 15872);
}
