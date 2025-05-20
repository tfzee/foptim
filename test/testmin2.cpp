// RUN: clang++ -fno-exceptions -O3 -mllvm -disable-llvm-optzns %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
// 
#include <vector>
#define N 1024
   void init_memory_float(float *start, float *end) {
   float state = 1.0;
   while (start != end) {     state *= 1.1;     *start = state;     start++;   }
 }
  unsigned digest_memory(void *start, void *end) {
   unsigned state = 1;
   while (start != end) {     state *= 3;     state ^= *((unsigned char *)start);     state = (state >> 8 ^ state << 8);     start = ((char *)start) + 1;   }
   return state;
 }
#define BENCH(NAME, RUN_LINE, ITER, DIGEST_LINE)                               \
  {                                                                            \
    unsigned r = DIGEST_LINE;                                                  \
    results.push_back(r);                                                      \
  }
int main(int argc, char *argv[]) {
   bool print_times = argc > 1;
   std::vector<unsigned> results;
   unsigned dummy = 0;
   const int Mi = 1 << 18;
   return 0;
 }
