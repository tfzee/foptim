// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Pfannkuchen(11) = 51 Result:0

#include <cstdio>
#include <cstdlib>
#include <iostream>

#define Int int
#define Aint int

static long fannkuch(int n) {
  Aint *perm;
  Aint *perm1;
  Aint *count;
  long flips;
  long flipsMax;
  Int r;
  Int i;
  Int k;
  Int didpr;
  const Int n1 = n - 1;

  if (n < 1)
    return 0;

  perm = (int *)calloc(n, sizeof(*perm));
  perm1 = (int *)calloc(n, sizeof(*perm1));
  count = (int *)calloc(n, sizeof(*count));

  for (i = 0; i < n; ++i)
    perm1[i] = i; /* initial (trivial) permu */

  r = n;
  didpr = 0;
  flipsMax = 0;
  for (;;) {
    if (didpr < 30) {
      // for (i = 0; i < n; ++i)
      //   printf("%d", (int)(1 + perm1[i]));
      // printf("\n");
      ++didpr;
    }
    for (; r != 1; --r) {
      count[r - 1] = r;
    }

#define XCH(x, y)                                                              \
  {                                                                            \
    Aint t_mp;                                                                 \
    t_mp = (x);                                                                \
    (x) = (y);                                                                 \
    (y) = t_mp;                                                                \
  }

    if (!(perm1[0] == 0 || perm1[n1] == n1)) {
      flips = 0;
      for (i = 1; i < n; ++i) { /* perm = perm1 */
        perm[i] = perm1[i];
      }
      k = perm1[0]; /* cache perm[0] in k */
      do {          /* k!=0 ==> k>0 */
        Int j;
        for (i = 1, j = k - 1; i < j; ++i, --j) {
          XCH(perm[i], perm[j])
        }
        ++flips;
        /*
         * Now exchange k (caching perm[0]) and perm[k]... with care!
         * XCH(k, perm[k]) does NOT work!
         */
        j = perm[k];
        perm[k] = k;
        k = j;
      } while (k);
      if (flipsMax < flips) {
        flipsMax = flips;
      }
    }

    for (;;) {
      if (r == n) {
        return flipsMax;
      }
      /* rotate down perm[0..r] by one */
      {
        Int perm0 = perm1[0];
        i = 0;
        while (i < r) {
          k = i + 1;
          perm1[i] = perm1[k];
          i = k;
        }
        perm1[r] = perm0;
      }
      if ((count[r] -= 1) > 0) {
        break;
      }
      ++r;
    }
  }
}

int main() {
  int n = 11;
  std::cout << "Pfannkuchen(" << n << ") = " << fannkuch(n) << "\n";
  return 0;
}
