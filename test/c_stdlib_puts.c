// RUN: clang -O0 %s -o %t.ll -S -emit-llvm
// RUN: %foffcc %t.ll %t.ss
// RUN: nasm %t.ss -felf64 -g -F dwarf && gcc %t.o -o %t.out
// RUN: %t.out | FileCheck %s

// CHECK: aaaa

void puts(const char*);

// int initialize_standard_library(int argc, char** argv, char** argp);

int main() {
  // initialize_standard_library(argc, argv, argp);
  puts("aaaa");
  return 0;
}

