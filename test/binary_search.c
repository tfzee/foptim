// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out -ecrt0
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

int binarySearch(int* array, int valueToFind, int length) {
  int pos = 0;
  int upper_limit = length;
  while (pos < upper_limit) {
    int currpos = pos + ((upper_limit - pos) >> 1);
    if (array[currpos] < valueToFind){
      pos = currpos + 1;
    }else{
      upper_limit = currpos;
    }
  }
  return (pos < length && array[pos] == valueToFind);
}

int main() {
  int arr[] = {5, 15, 24, 32, 56, 89};
  int size_of_array = sizeof(arr) / sizeof(int);
  if (binarySearch(arr, 24, size_of_array) == 0) {
    return 1;
  }
  // if (binarySearch(arr, 100, size_of_array)) {
  //   return 2;
  // }
  // return 0;
  return 0;
}
