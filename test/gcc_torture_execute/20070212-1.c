// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: %t.out | echo Result:$? | FileCheck %s

// CHECK: Result:0

struct f {
  int i;
};

int g(int i, int c, struct f *ff, int *p) {
  int *t;
  if (c) {
    t = &i;
  } else {
    t = &ff->i;
  }
  *p = 0;
  return *t;
}

int main() {
  struct f f;
  f.i = 1;
  if (g(5, 0, &f, &f.i) != 0) {
    return 1;
  }
  return 0;
}
