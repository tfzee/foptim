// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK-NOT: failed
// CHECK: Result:0


#include <math.h>
#include <stdio.h>

#define init_value 1.0
#define SIZE 8000

int current_test = 0;
float dataFloat[SIZE];

template <typename Iterator, typename T>
static void fill(Iterator first, Iterator last, T value) {
  while (first != last)
    *first++ = value;
}

bool tolerance_equal(float &a, float &b) {
  float diff = a - b;
  double reldiff = diff;
  if (fabs(a) > 1.0e-4)
    reldiff = diff / a;
  return (fabs(reldiff) < 1.0e-3);
}
template <typename Shifter> inline void check_shifted_sum(float result) {
  float temp = (float)SIZE * Shifter::do_shift((float)init_value);
  if (!tolerance_equal(result, temp))
    printf("test %i failed\n", current_test);
}
template <typename T> struct custom_constant_divide {
  static T do_shift(T input) { return (input / T(5)); }
};
template <typename T> struct custom_multiple_constant_divide {
  static T do_shift(T input) {
    return ((((input / T(2)) / T(3)) / T(4)) / T(5));
  }
};
template <typename T> struct custom_multiple_constant_mixed {
  static T do_shift(T input) { return (input + T(2) - T(3) * T(4) / T(5)); }
};
template <typename T, typename Shifter>
static void test_constant(T *first, int count, const char *label) {
  int i;
  T result = 0;
  for (int n = 0; n < count; ++n) {
    result += Shifter::do_shift(first[n]);
  }
  check_shifted_sum<Shifter>(result);
  current_test++;
}

int main() {
  ::fill(dataFloat, dataFloat + SIZE, float(init_value));

  test_constant<float, custom_constant_divide<float>>(dataFloat, SIZE,
                                                      "float constant divide");

  test_constant<float, custom_multiple_constant_divide<float>>(
      dataFloat, SIZE, "float multiple constant divides");
  test_constant<float, custom_multiple_constant_mixed<float>>(
      dataFloat, SIZE, "float multiple constant mixed");
  return 0;
}
