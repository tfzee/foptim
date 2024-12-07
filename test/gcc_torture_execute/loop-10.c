// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

static int count = 0;

static void inc(void) { count++; }

int main(void) {
  int iNbr = 1;
  int test = 0;
  while (test == 0) {
    inc();
    if (iNbr == 0) {
      break;
    } else {
      inc();
      iNbr--;
    }
    test = 1;
  }
  if (count != 2) {
    return 1;
  }
  return 0;
}
