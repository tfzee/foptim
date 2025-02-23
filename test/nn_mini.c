// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#include <stdlib.h>
typedef double (*genann_actfun)(const struct genann *ann, double a);
typedef struct genann {
  int inputs, hidden_layers, hidden, outputs;
  genann_actfun activation_hidden;
  genann_actfun activation_output;
  int total_weights;
  int total_neurons;
  double *weight;
  double *output;
  double *delta;
} genann;

void genann_randomize(genann *ann) {
  int i;
  for (i = 0; i < ann->total_weights; ++i) {
    double r = (((double)rand()) / RAND_MAX);
    ann->weight[i] = r - 0.5;
  }
}
genann *genann_init(int inputs, int hidden_layers, int hidden, int outputs) {
  const int hidden_weights =
      hidden_layers
          ? (inputs + 1) * hidden + (hidden_layers - 1) * (hidden + 1) * hidden
          : 0;
  const int output_weights =
      (hidden_layers ? (hidden + 1) : (inputs + 1)) * outputs;
  const int total_weights = (hidden_weights + output_weights);
  const int total_neurons = (inputs + hidden * hidden_layers + outputs);
  const int size =
      sizeof(genann) + sizeof(double) * (total_weights + total_neurons +
                                         (total_neurons - inputs));
  genann *ret = (genann *)malloc(size);
  ret->inputs = inputs;
  ret->hidden_layers = hidden_layers;
  ret->hidden = hidden;
  ret->outputs = outputs;
  ret->total_weights = total_weights;
  ret->total_neurons = total_neurons;
  ret->weight = (double *)((char *)ret + sizeof(genann));
  ret->output = ret->weight + ret->total_weights;
  ret->delta = ret->output + ret->total_neurons;
  genann_randomize(ret);
  return ret;
}
int main() {
  genann *ann = genann_init(2, 1, 2, 1);
  return 0;
}
