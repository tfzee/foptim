// Regression test: when one instruction has several spilled vregs (here the
// base of `[$2 + 8]` and its written result) the spiller used to skip the later
// ones after inserting the store of the first, so the base was reloaded from a
// stack slot that was never written (garbage pointer, heap corruption).
// RUN: clang++ -fno-exceptions -O3 -mllvm -disable-llvm-optzns -std=c++20 %s -S -emit-llvm -o %t.ll
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out -static-libstdc++
// RUN: %t.out | FileCheck %s

#include <cstdio>
#include <string>

__attribute__((noinline)) void sink(std::string s, int k) {
  printf("%d %s\n", k, s.c_str());
}

__attribute__((noinline)) void f(std::string label) {
  sink(label + " std::lower_bound", 1);
  sink(label + " lower_bound1", 2);
  sink(label + " lower_bound2", 3);
  sink(label + " lower_bound_recursive", 4);
}

int main() {
  std::string l = "int8_t 5 pointer";
  f(l);
  return 0;
}

// CHECK: 1 int8_t 5 pointer std::lower_bound
// CHECK-NEXT: 2 int8_t 5 pointer lower_bound1
// CHECK-NEXT: 3 int8_t 5 pointer lower_bound2
// CHECK-NEXT: 4 int8_t 5 pointer lower_bound_recursive
