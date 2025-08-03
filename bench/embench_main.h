#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define register
#define CPU_MHZ 5000
void initialise_board() {}
[[clang::noinline, gnu::noinline]] void start_trigger() {}
[[clang::noinline, gnu::noinline]] void stop_trigger() {}
void srand_beebs(int i) { srand(i); }
int rand_beebs(void) { return rand(); }
void assert_beebs(int b) {
  if (b != 0) {
    abort();
  }
}
void initialise_benchmark(void);
void warm_caches(int heat);
int benchmark(void) __attribute__((noinline));
int verify_benchmark(int res);

void free_beebs(void *p) { free(p); }

int double_eq_beebs(double a, double b) { return a - b < 1e-4; }
int double_neq_beebs(double a, double b) { return !double_eq_beebs(a, b); }
int float_eq_beebs(float a, float b) { return a - b < 1e-4; }
int float_neq_beebs(float a, float b) { return !float_eq_beebs(a, b); }

void init_heap_beebs(void *out, size_t size) {
  (void)out;
  (void)size;
}
void *calloc_beebs(size_t nmemb, size_t size) { return calloc(nmemb, size); }
void *malloc_beebs(size_t size) { return malloc(size); }

int __attribute__((used)) main(int argc __attribute__((unused)),
                               char *argv[] __attribute__((unused))) {
  volatile int result;
  int correct;

  initialise_benchmark();
  warm_caches(1);
  result = benchmark();
  correct = verify_benchmark(result);
  if (!correct) {
    printf("Failed!");
  } else {
    printf("Succ!");
  }
  return !correct;
}
