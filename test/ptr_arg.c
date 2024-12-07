// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:12

int test(int* a){
  *a += 1;
  return *a;
}


int main() {
  int a = 5;
  int b =  test(&a);
  return a + b; 
}
