// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

unsigned sat_add(unsigned i) {
  unsigned ret = i + 1;
  if (ret < i) {
    ret = i;
  }
  return ret;
}

unsigned sat_add2(unsigned i) {
  unsigned ret = i + 1;
  if (ret > i) {
    return ret;
  }
  return i;
}

unsigned sat_add3(unsigned i) {
  unsigned ret = i - 1;
  if (ret > i) {
    ret = i;
  }
  return ret;
}

unsigned sat_add4(unsigned i) {
  unsigned ret = i - 1;
  if (ret < i) {
    return ret;
  }
  return i;
}

int main() {
  if (sat_add(~0U) != ~0U) {
    return 1;
  }
  if (sat_add2(~0U) != ~0U) {
    return 2;
  }
  if (sat_add3(0U) != 0U) {
    return 3;
  }
  if (sat_add4(0U) != 0U) {
    return 4;
  }
  return 0;
}
