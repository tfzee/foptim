// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

//TODO: these nans are not perfectly the same
// CHECK: -1 0.833333 0 1 0 0 0 0.3125 0 0.761968 0 -nan 0 -nan 0 nan 0 -nan 0 nan -0.833333 1 0.0277778 0 0 0 0 0 0 0 0 -nan 0 -nan 0 nan 0 -nan 0 nan -0.666667 -0 0.0555555 6 2.38419e-07 2 0 2.18182 0 -5.44682 0 -nan 0 -nan 0 nan 0 -nan 0 nan -0.5 0.166667 0.0833333 5 2.5332e-07 1 -8.19564e-08 1.09091 0 -0.723408 0 -nan 0 -nan 0 nan 0 -nan 0 nan -0.333333 0.333333 0.111111 4 4.61936e-07 1.3125 -5.21541e-08 0.806818 3.18343e-08 1.27659 0 -nan 0 -nan 0 nan 0 -nan 0 nan -1 0.5 0 3 0 0.5 0 0.795455 0 0.0478708 0 -nan 0 -nan 0 nan 0 -nan 0 nan -0.833333 0.666667 0.0277778 2 4.47035e-08 1 -2.23517e-08 -0.159091 -2.57384e-08 -0.271281 -3.89391e-08 -nan -nan -nan 0 nan 0 -nan 0 nan -0.666667 0.833333 0.0555555 1 2.38419e-07 0 0 0.3125 0 0.761968 0 -nan -nan -nan nan nan 0 -nan 0 nan -0.5 1 0.0833333 0 2.5332e-07 0 -8.19564e-08 0 -2.79397e-08 0 -3.5113e-08 -nan -nan -nan nan nan -nan -nan 0 nan -0.333333 -0 0.111111 6 4.61936e-07 2 -5.21541e-08 2.18182 3.18343e-08 -5.44682 3.5221e-08 -nan -nan -nan nan nan -nan -nan nan nan
// CHECK: Result:0

#include <iostream>
#define N 100

void LUdecomposition(float a[N][N], float l[N][N], float u[N][N], int n) {
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      if (j < i)
        l[j][i] = 0;
      else {
        l[j][i] = a[j][i];
        for (int k = 0; k < i; k++) {
          l[j][i] = l[j][i] - l[j][k] * u[k][i];
        }
      }
    }
    for (int j = 0; j < n; j++) {
      if (j < i)
        u[i][j] = 0;
      else if (j == i)
        u[i][j] = 1;
      else {
        u[i][j] = a[i][j] / l[i][i];
        for (int k = 0; k < i; k++) {
          u[i][j] = u[i][j] - ((l[i][k] * u[k][j]) / l[i][i]);
        }
      }
    }
  }
}

int main() {
  float a[N][N];
  float l[N][N];
  float u[N][N];

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      a[i][j] = (i % 5 + j % 7) / 6.F - 1.F;
    }
  }
  LUdecomposition(a, l, u, N);

  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      std::cout << l[i][j] << " ";
      std::cout << u[j][N - 1 - i] << " ";
    }
  }
  std::cout << std::endl;
  return 0;
}
