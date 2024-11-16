// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0

static int ap(int i);
static void testit(void) {
  int ir[4] = {0, 1, 2, 3};
  int ix, n, m;
  n = 1;
  m = 3;
  for (ix = 1; ix <= 4; ix++) {
    if (n == 1)
      m = 4;
    else
      m = n - 1;
    ap(ir[n - 1]);
    n = m;
  }
}

static int t = 0;
static int a[4];

static int ap(int i) {
  // if (t > 3) {
  //   return 0
  // }
  a[t++] = i;
  return 1;
}

int main(void) {
  testit();
  if (a[0] != 0)
    return 1;
  if (a[1] != 3)
    return 1;
  if (a[2] != 2)
    return 1;
  if (a[3] != 1)
    return 1;
  return 0;
}
