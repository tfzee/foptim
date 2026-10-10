; RUN: %foffcc %s --print-fir --passes "InstSimplify" | FileCheck %s
; Regression test: constant folding lshr of a negative i64 must be a logical shift.

; CHECK: And(%0, 1152921504606846975:i64)
define i64 @f(i64 %x) {
  %m = lshr i64 -1, 4
  %r = and i64 %x, %m
  ret i64 %r
}
