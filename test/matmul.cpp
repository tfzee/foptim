// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: ../build/foptim_main %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:1

#define SIZE 1024

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
  int a[SIZE * SIZE] = {};
  int b[SIZE * SIZE] = {};
  for (int i = 0; i < SIZE * SIZE; i++) {
    a[i] = i % 9;
    b[i] = i % 5;
  }
  int c[SIZE * SIZE] = {};

  matmul(a, b, c, SIZE);

  int sum = 0;
  for (int i : c) {
    sum += i;
  }
  return static_cast<int>(sum == 16773628);
}
