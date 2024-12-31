// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:5

long long get_gcd_recursive(long long a, long long b) {
  if (a == 0) {
    return b;
  } else if (b == 0) {
    return a;
  }

  if (b > a) {
    a = a - b;
    b = a + b;
    a = b - a;
  }
  long long remainder = a % b;
  if (remainder == 0) {
    return b;
  }
  return get_gcd_recursive(b, remainder);
}

int main() {
  long long a = 4325;
  long long b = 5435;
  return get_gcd_recursive(a, b);
}
