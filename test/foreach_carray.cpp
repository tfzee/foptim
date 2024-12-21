// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out -ecrt0
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:21

#define LEN 128

extern "C" int main() {
  int c[LEN] = {5,4,5,2,5};

  int sum = 0;
  for (int i : c) {
    sum += i;
  }
  return sum;
}

