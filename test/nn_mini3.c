// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#include <stdlib.h>

double genann_act_sigmoid_cached(double a) {
  if ((size_t)(a + 0.5) >= 4096)
    return 3.0;

  return 0.0;
}

double const *genann_run(double *output) {
  double *o = output;
  for (int j = 0; j < 1; ++j) {
    for (int k = 0; k < 0; ++k) {
    }
    *o++ = genann_act_sigmoid_cached(0);
  }
  return o;
}

int main() {
  double *ret = (double *)malloc(136);
  genann_run(ret);
  return 0;
}
