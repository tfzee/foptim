// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out -ecrt0
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:42

#define LEN 32

void axpy(const int *a, const int *b, const int *y, int *c, unsigned int len) {
  for (unsigned int i = 0; i < len; i++) {
    c[i] = a[i] * b[i] + y[i];
  }
}

extern "C" int main() {
  int a[LEN] = {1, 2, 3, 4, 5};
  int b[LEN] = {1, 2, 1, 2, 1};
  int y[LEN] = {5, 5, 3, 4, 4};
  int c[LEN] = {};

  axpy(a, b, y, c, LEN);

  int sum = 0;
  for (int i : c) {
    sum += i;
  }
  return sum;
}
