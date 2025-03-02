// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:207

struct BigStruct {
  int a;
  int b;
  int c;
  int d;
  int e;
  int f;
};

int main() {
  struct BigStruct s2;
  s2.a = 11;
  s2.b = 1;
  s2.c = 2;
  s2.d = 3;
  s2.e = 4;
  s2.f = 5;

  int res = 0;
  for (int i = 0; i < 32; i++) {
    if (i % 2 == 0) {
      s2.a = s2.b + s2.d - res;
      res += s2.b + s2.d + s2.f;
    } else {
      s2.b = s2.a + s2.c - res;
      res += s2.a + s2.c + s2.e;
    }
  }

  return res;
}
