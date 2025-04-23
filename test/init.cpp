// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0


#include <stdio.h>
template <typename T> struct PointerWrapper {
  T *current;
  PointerWrapper(T *x) : current(x) {}
  T &operator*() const { return *current; }
};
template <typename T>
inline PointerWrapper<T> operator++(PointerWrapper<T> &xx, int) {
  PointerWrapper<T> tmp = xx;
  ++xx.current;
  return tmp;
}
template <typename T>
inline bool operator!=(const PointerWrapper<T> &x, const PointerWrapper<T> &y) {
  return (x.current != y.current);
}
#define SIZE 2000
double init_value = 3.0;
double data[SIZE];
double *dpb = data;
double *dpe = data + SIZE;
PointerWrapper<double> dPb(dpb);
PointerWrapper<double> dPe(dpe);

int main() {
  double dZero = 0.0;
  auto first = dPb;
  while (first != dPe) {
    first++;
    dZero = dZero + init_value;
  }

  if (dZero != SIZE * init_value) {
    printf("test failed %f != %f\n", dZero, SIZE * init_value);
  }
  return 0;
}
