// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.o
// RUN: clang %t.o -o %t.out
// RUN: result=$(bash -c '(%t.out); echo Result:$?' 2>&1)
// RUN: echo $result | FileCheck %s

// CHECK: Result:0

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef genann_act
#define genann_act_hidden genann_act_hidden_indirect
#define genann_act_output genann_act_output_indirect
#else
#define genann_act_hidden genann_act
#define genann_act_output genann_act
#endif
#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define unused __attribute__((unused))
#else
#define likely(x) x
#define unlikely(x) x
#define unused
#pragma warning(disable : 4996) /* For fscanf */
#endif

#define LOOKUP_SIZE 4096

const double sigmoid_dom_min = -15.0;
const double sigmoid_dom_max = 15.0;
double interval;
double lookup[LOOKUP_SIZE];

struct genann;

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

double const *genann_run(genann const *ann, double const *inputs);

double genann_act_hidden_indirect(const struct genann *ann, double a) {
  return ann->activation_hidden(ann, a);
}

double genann_act_output_indirect(const struct genann *ann, double a) {
  return ann->activation_output(ann, a);
}

double genann_act_sigmoid_cached(const genann *ann unused, double a) {
  assert(!isnan(a));

  if (a < sigmoid_dom_min)
    return lookup[0];
  if (a >= sigmoid_dom_max)
    return lookup[LOOKUP_SIZE - 1];

  size_t j = (size_t)((a - sigmoid_dom_min) * interval + 0.5);

  /* Because floating point... */
  if (unlikely(j >= LOOKUP_SIZE))
    return lookup[LOOKUP_SIZE - 1];

  return lookup[j];
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

  /* Allocate extra size for weights, outputs, and deltas. */
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

  /* Set pointers. */
  ret->weight = (double *)((char *)ret + sizeof(genann));
  ret->output = ret->weight + ret->total_weights;
  ret->delta = ret->output + ret->total_neurons;
  ret->activation_hidden = genann_act_sigmoid_cached;
  ret->activation_output = genann_act_sigmoid_cached;

  return ret;
}

double const *genann_run(genann const *ann, double const *inputs) {
  double const *w = ann->weight;
  double *o = ann->output + ann->inputs;
  double const *i = ann->output;

  memcpy(ann->output, inputs, sizeof(double) * ann->inputs);

  int h, j, k;

  if (!ann->hidden_layers) {
    double *ret = o;
    for (j = 0; j < ann->outputs; ++j) {
      double sum = *w++ * -1.0;
      for (k = 0; k < ann->inputs; ++k) {
        sum += *w++ * i[k];
      }
      *o++ = genann_act_output(ann, sum);
    }

    return ret;
  }

  /* Figure input layer */
  for (j = 0; j < ann->hidden; ++j) {
    double sum = *w++ * -1.0;
    for (k = 0; k < ann->inputs; ++k) {
      sum += *w++ * i[k];
    }
    *o++ = genann_act_hidden(ann, sum);
  }

  i += ann->inputs;

  /* Figure hidden layers, if any. */
  for (h = 1; h < ann->hidden_layers; ++h) {
    for (j = 0; j < ann->hidden; ++j) {
      double sum = *w++ * -1.0;
      for (k = 0; k < ann->hidden; ++k) {
        sum += *w++ * i[k];
      }
      *o++ = genann_act_hidden(ann, sum);
    }

    i += ann->hidden;
  }

  double const *ret = o;

  /* Figure output layer. */
  for (j = 0; j < ann->outputs; ++j) {
    double sum = *w++ * -1.0;
    for (k = 0; k < ann->hidden; ++k) {
      sum += *w++ * i[k];
    }
    *o++ = genann_act_output(ann, sum);
  }

  return ret;
}

int main() {

  const double input[4][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};

  genann *ann = genann_init(2, 1, 2, 1);

  genann_run(ann, input[0]);
  return 0;
}
