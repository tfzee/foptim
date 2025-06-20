// RUN: clang++ -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang++ -static-libstdc++ %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0
#include <iostream>

typedef struct genann {
  int inputs;
  double *weight;
  double *output;
} genann;

double const *genann_run(genann const *ann, double const *inputs) {
  return ann->output;
}

void print_out(genann *ann) {
  const double input[4][2] = {{0, 0}, {0, 1}, {1, 1}};
  std::cout << " " << input[0][0] << " " << input[0][1] << " "
            << *genann_run(ann, input[0]) << " ";
  std::cout << " " << input[1][0] << " " << input[1][1] << " "
            << *genann_run(ann, input[1]) << " ";
  std::cout << " " << input[2][0] << " " << input[2][1] << " "
            << *genann_run(ann, input[2]) << " ";
  std::cout << " " << input[3][0] << " " << input[3][1] << " " << 0 << "\n";
}

int main() {
  const int size = sizeof(genann) + sizeof(double) * 11;
  genann *ret = (genann *)malloc(size);
  ret->output = (double *)((char *)ret + sizeof(genann)) + 9;
  print_out(ret);
}
