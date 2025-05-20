// RUN: clang++ -fno-exceptions -O3 -mllvm -disable-llvm-optzns %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
//
#include <algorithm>
#include <stdio.h>

using namespace std;
template <class Iterator> void verify_sorted(Iterator first, Iterator last) {
  Iterator prev = first;
  first++;
  while (first != last) {
    if (*first++ < *prev++) {
      break;
    }
  }
}

int less_than_function1(const void *lhs, const void *rhs) { return 0; };

int main() {
  int tablesize = 1000;
  double *master_table = new double[tablesize];
  double *table = new double[tablesize];
  copy(master_table, master_table + tablesize, table);
  qsort(table, tablesize, sizeof(double), less_than_function1);
  verify_sorted(table, table + tablesize);
  delete[] master_table;
  return 0;
}
