// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

static int ap(int i) { return 1; }

static void testit() {
  int ir[4] = {0, 1, 2, 3};
  int ix, n, m;
  n = 1;
  m = 1;
  for (ix = 1; ix <= 4; ix++) {
    ap(ir[n - 1]);
    n = m;
  }
}

int main(void) { testit(); return 0; }

