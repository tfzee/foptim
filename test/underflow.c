// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

unsigned char test(unsigned char a) {
  unsigned char res = a - 3;
  if (res != 255) {
    return 2;
  }
  return 0;
}

int main() {
  unsigned char a = 2;
  unsigned char res = a - 3;

  if (res != 255) {
    return 1;
  }
  return test(a);
}
