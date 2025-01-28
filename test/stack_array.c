// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#define u64 unsigned long

#define X 64

u64 bench() {
  u64 count = 0;
  unsigned char a[X] = {0};
  for (u64 i = 0; i < X; i += 2) {
    a[i] = 1;
  }
  for (u64 i = 0; i < X; i++) {
    count += a[i];
  }
  return count;
}

int main() {
  const u64 res = bench();
  if (res == X / 2) {
    return 0;
  }
  return 33;
}
