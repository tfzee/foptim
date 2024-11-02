// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: ../build/foptim_main %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0

#define u64 unsigned long

#define X 2

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
