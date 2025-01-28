// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:55

static void ap(int i);
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

static void ap(int i) {
  // if (t > 3) {
  //   return 0
  // }
  a[t++] = i;
  // return 1;
}

int main(void) {
  t = 0;
  testit();
  if (a[0] != 0)
    return 1;
  if (a[1] != 3)
    return 2;
  if (a[2] != 2)
    return 3;
  if (a[3] != 1)
    return 4;
  return 55;
}
