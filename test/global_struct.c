// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:49

typedef struct{
  int a;
  int b;
} Vec2;


Vec2 a;

int main() {
  a.a = 36;
  a.b = 13;
  return a.a + a.b;
}
