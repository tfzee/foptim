// RUN: clang -O0 %s -o %t.ll -S -emit-llvm 
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64  -g -F dwarf && ld %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#define u64 unsigned long

#define X 1048576

u64 bench() {
    u64 count = 0;
    u64 n = X + 1;
    unsigned char a[X+1] = {};
    if (n > 1) {
        a[0] = a[1] = 1;
        for (u64 i = 4; i < n; i += 2) {
            a[i] = 1;
        }
        for (u64 p = 3; p * p < n; p += 2) {
            // for all potential prime numbers
            if (a[p] == 0) {
                // if p is prime, flag all odd multiples of p as composite
                for (u64 i = p * p; i < n; i += 2 * p) {
                    a[i] = 1;
                }
            }
        }
        count = n;
        for (u64 i = 0; i < n; i++) {
            count -= a[i];
        }
    }
    return count;
}

int main(){
  const u64 res = bench();
  if((res == 82025)){
    return 0;
  }
  return 33;
}
