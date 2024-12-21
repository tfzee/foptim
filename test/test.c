// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out -ecrt0
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:13

int test(int a, int b){
  return a+b;
}


int main() {
  int a = 5;
  int d = 0;
  const int c = 2;
  while (a < 10) {
    d = 1 + c;
    a = a + 1;
    test(d, a);
  }
  return a + d;
}
