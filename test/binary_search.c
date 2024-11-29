// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:0
// XFAIL: *

int binarySearch(int array[], int number, int start, int end) {
  if (start >= end) {
    return array[start] == number ? 0 : 1;
  }

  int tmp = (int)end / 2;
  if (number == array[tmp]) {
    return 0;
  } else if (number > array[tmp]) {
    return binarySearch(array, number, start, tmp);
  } else {
    return binarySearch(array, number, tmp, end);
  }
}

int main() {
  int arr[] = {5, 15, 24, 32, 56, 89};
  int size_of_array = sizeof(arr) / sizeof(int);
  if (binarySearch(arr, 24, 0, size_of_array - 1) == 1) {
    return 1;
  }
  if (binarySearch(arr, 100, 0, size_of_array - 1) == 0) {
    return 2;
  }
  return 0;
}
