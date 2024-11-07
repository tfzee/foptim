// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:26

struct BigStruct {
  int a;
  int b;
  int c;
  int d;
  int e;
  int f;

  int sumit() { return a + b + c + d + e + f; }
};

extern "C" int main() {
  struct BigStruct s2;
  s2.a = 11;
  s2.b = 1;
  s2.c = 2;
  s2.d = 3;
  s2.e = 4;
  s2.f = 5;

  return s2.sumit();
}
