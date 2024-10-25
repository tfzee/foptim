// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: ../build/foptim_main %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

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
