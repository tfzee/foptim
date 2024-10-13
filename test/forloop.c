// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: ../build/foptim_main %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: %t.out || echo Result:$? | FileCheck %s

// CHECK: Result:28


int main(){
  int sum = 0;
  for(int i = 0; i < 8; i++){
    sum += i;
  }
  return sum;
}

