// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:20

#define u64 unsigned long
#define u32 unsigned int

#define X 32

u64 bench(u32 multiplier) {
  u64 count = 0;
  for (u64 i = 0; i < X; i++) {
    count += i * (multiplier + X) * 2;
  }
  return count;
}

int main() {
  const u64 res = bench(5);
  if (res == 36704) {
    return 20;
  }
  return 33;
}
