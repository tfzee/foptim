// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s


#include <iostream>
#define N 500

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
