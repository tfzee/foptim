// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#include <stdlib.h>

double genann_act_sigmoid() { return 1.0; }

double const *genann_run(double *o) {
  for (int j = 0; j < 1; ++j) {
    for (int k = 0; k < 0; ++k) {
    }
    *o++ = genann_act_sigmoid();
  }
  return o;
}

int main() {
  double *ret = (double *)malloc(146);
  genann_run(ret);
  return 0;
}

