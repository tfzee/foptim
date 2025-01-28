// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:64

#define LEN 128

extern "C" int main() {
  int a[LEN] = {1, 2, 3, 4, 5};
  a[LEN / 2] = 51;

  int res = 0;
  for (int i = 0; i < LEN; i++) {
    if (a[i] == 51) {
      res = i;
      break;
    }
  }

  return res;
}
