// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:1

int main() {
  double zi = 0.0;
  bool notDone = true;
  int z = 0;
  int escape = 0;
  do {
    zi = zi + -1.0;

    if (zi * zi > 1.0) {
      notDone = false;
      escape = 1;
    }
    z += 1;
  } while (notDone && z < 2);

  return escape;
}
