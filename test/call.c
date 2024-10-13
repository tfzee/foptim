// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: ../build/foptim_main %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:37

int test(int a, int b){
  return a + b;
}


int main() {
  int a = 5;
  int d = 32;
  return test(a, d);
}
