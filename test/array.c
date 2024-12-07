// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:56

#define size 8

int main(){
  int arr[size];  
  
  for(int i = 0; i < size; i++){
    arr[i] = i*2;
  }

  int sum = 0;
  for(int i = 0; i < size; i++){
    sum += arr[i];
  }
  return sum;
}

