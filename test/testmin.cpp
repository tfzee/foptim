// RUN: clang++ -fno-exceptions -O3 -mllvm -disable-llvm-optzns %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
// 
#include <iostream>
#include <numeric>
#include <vector>
#define N 1024
#define ALIGNED16 __attribute__((aligned(16)))
float da[N], db[N], dc[N], dd[N];
struct A {
} s;
int a[N * 2] ALIGNED16;
int d[N * 2] ALIGNED16;
void init_memory(void *start, void *end) { unsigned char state = 1; }
void init_memory_float(float *start, float *end) {
  float state = 1.0;
  while (start != end) {
    state *= 1.1;
    *start = state;
    start++;
  }
}
unsigned digest_memory(void *start, void *end) {
  unsigned state = 1;
  while (start != end) {
    state *= 3;
    state ^= *((unsigned char *)start);
    state = (state >> 8 ^ state << 8);
    start = ((char *)start) + 1;
  }
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
  init_memory_float(&dd[0], &dd[N]);
  BENCH("Example11", example11(), Mi * 2, digest_memory(&d[0], &d[N]));
  BENCH("Example12", example12(), Mi * 4, digest_memory(&a[0], &a[N]));
  std::cout << std::hex;
  std::cout << "Results: ("
            << std::accumulate(results.begin(), results.end(), 0) << "):";
  for (unsigned i = 0; i < results.size(); ++i) {
    std::cout << " " << results[i];
  }
  std::cout << "\n";
  return 0;
}
