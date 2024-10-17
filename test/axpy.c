// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: ../build/foptim_main %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:42

#define LEN 128

void axpy(const int *a, const int *b, const int *y, int *c, unsigned int len) {
  for (unsigned int i = 0; i < len; i++) {
    c[i] = a[i] * b[i] + y[i];
  }
}

int main() {
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
