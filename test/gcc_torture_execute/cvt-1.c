// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
#include <stdlib.h>


static inline long
g1 (double x)
{
  return (double) (long) x;
}

long
g2 (double f)
{
  return f;
}

double
f (long i)
{
  if (g1 (i) != g2 (i))
    abort ();
  return g2 (i);
}

int
main (void)
{
  if (f (123456789L) != 123456789L)
    abort ();
  if (f (123456789L) != g2 (123456789L))
    abort ();
  exit (0);
}
