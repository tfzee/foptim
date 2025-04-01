// RUN: clang++ -fno-exceptions -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: 432432okakResult:0


#include <string>
#include <iostream>



int main(){
  std::string test = "432432";
  test += "okak";

  std::cout << test;
  return 0;
}
