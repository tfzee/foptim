// RUN: clang -O3 -mllvm -disable-llvm-optzns %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s
//
// CHECK: Result:1



enum Data : unsigned char {
  eA = 15,
  eB = 30,
  eC = 60,
};

int test(enum Data v) {
  switch (v) {
  case eA:
    return 1;
  case eB:
    return 32;
  case eC:
  default:
    return 0;
  }
}

int main() {
  enum Data b = eA;
  return test(b);
}
