#include <iostream>

using namespace std;
#define N 10

void LUdecomposition(float a[N][N], float l[N][N], float u[N][N], int n) {
  int i = 0, j = 0, k = 0;
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      if (j < i)
        l[j][i] = 0;
      else {
        l[j][i] = a[j][i];
        for (k = 0; k < i; k++) {
          l[j][i] = l[j][i] - l[j][k] * u[k][i];
        }
      }
    }
    for (j = 0; j < n; j++) {
      if (j < i)
        u[i][j] = 0;
      else if (j == i)
        u[i][j] = 1;
      else {
        u[i][j] = a[i][j] / l[i][i];
        for (k = 0; k < i; k++) {
          u[i][j] = u[i][j] - ((l[i][k] * u[k][j]) / l[i][i]);
        }
      }
    }
  }
}

int main() {
  float a[N][N], l[N][N], u[N][N];

  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++)
      a[i][j] = (i % 5 + j % 6) / 20.F - 10.f;
  LUdecomposition(a, l, u, N);
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      cout << l[i][j] << " ";
    }
    cout << endl;
  }
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      cout << u[i][j] << " ";
    }
    cout << endl;
  }
  return 0;
}

