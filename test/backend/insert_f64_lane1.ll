; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; CHECK: vpinsr
; RUN: %foffcc %s %t.o
; insertelement of a double into the upper lane used to hit an unimplemented vpinsr in the encoder

define void @f(ptr %p, double %x) {
  %v = load <2 x double>, ptr %p
  %w = insertelement <2 x double> %v, double %x, i32 1
  store <2 x double> %w, ptr %p
  ret void
}
