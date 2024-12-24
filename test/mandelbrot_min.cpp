// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

int mandelbrot(bool x, bool y, bool bitNum) {
  int sum = 0;
  while (y) {
    while (x) {
      if (bitNum) {
        sum = 1;
      }
    }
  }
  return sum;
}

int main(){
  return mandelbrot(false, false, false);
}
