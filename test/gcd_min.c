// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0


long get_gcd_recursive_a, get_gcd_recursive_b;
long get_gcd_recursive() {
  if (get_gcd_recursive_a)
    return get_gcd_recursive_b;
  if (get_gcd_recursive_a)
    ;
  return get_gcd_recursive_b;
}

int main(){
  return 0;
}
