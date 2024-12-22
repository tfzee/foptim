// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#define SIZE 256

void matmul(const int *a, const int *b, int *c, unsigned int size) {
  for (unsigned int i = 0; i < size; ++i) {
    for (unsigned int j = 0; j < size; ++j) {
      int t = 0;
      for (unsigned int k = 0; k < size; ++k) {
        t += a[i * size + k] * b[k * size + j];
      }
      c[i * size + j] = t;
    }
  }
}

extern "C" int main() {
  int a[SIZE * SIZE] = {0};
  int b[SIZE * SIZE] = {0};
  for (int i = 0; i < SIZE * SIZE; i++) {
    a[i] = i % 9;
    b[i] = i % 5;
  }
  int c[SIZE * SIZE] = {0};

  matmul(a, b, c, SIZE);

  int sum = 0;
  for (int i : c) {
    sum += i;
  }
  if(sum == 134212116){
    return 0;
  }
  return 33;
}
