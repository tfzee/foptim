// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

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
