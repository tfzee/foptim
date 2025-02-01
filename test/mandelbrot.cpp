// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:191
// innerIterations == 750   result == 50
// innerIterations == 500   result == 191
// innerIterations == 100   result == 239
// innerIterations == 50   result == 15
// innerIterations == 10   result == 127
// innerIterations == 2   result == 192
// innerIterations == 1   result == 128

static int mandelbrot(int size) {
  int sum = 0;
  int byteAcc = 0;
  int bitNum = 0;

  int y = 0;

  while (y < size) {
    double ci = (2.0 * y / size) - 1.0;
    int x = 0;

    while (x < size) {
      double zr = 0.0;
      double zrzr = 0.0;
      double zi = 0.0;
      double zizi = 0.0;
      double cr = (2.0 * x / size) - 1.5;

      int z = 0;
      bool notDone = true;
      int escape = 0;
      while (notDone && z < 50) {
        zr = zrzr - zizi + cr;
        zi = 2.0 * zr * zi + ci;

        // preserve recalculation
        zrzr = zr * zr;
        zizi = zi * zi;

        if (zrzr + zizi > 4.0) {
          notDone = false;
          escape = 1;
        }
        z += 1;
      }

      byteAcc = (byteAcc << 1) + escape;
      bitNum += 1;

      if (bitNum == 8) {
        sum ^= byteAcc;
        byteAcc = 0;
        bitNum = 0;
      } else if (x == size - 1) {
        byteAcc <<= (8 - bitNum);
        sum ^= byteAcc;
        byteAcc = 0;
        bitNum = 0;
      }
      x += 1;
    }
    y += 1;
  }
  return sum;
}

int main() {
  const int innerIterations = 500;
  return mandelbrot(innerIterations);
}
