// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
// XFAIL: *

static int a = 0;
extern int b __attribute__((alias("a")));
__attribute__((noinline)) static void inc(void) { b++; }

int main() {
  a = 0;
  inc();
  if (a != 1) {
    return 1;
  }
  return 0;
}
