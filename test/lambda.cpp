// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:3

template <class T> int test(T adder) { return adder(1, 2); }

int (*get_lambda())(int, int) {
  return [](int a, int b) { return a + b; };
}

int main() {
  auto adder = get_lambda();
  return test(adder);
}
