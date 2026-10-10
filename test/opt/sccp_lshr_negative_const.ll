; RUN: %foffcc %s --print-fir --passes "SCCP" | FileCheck %s
; Regression test: lshr of a sign extended constant must not shift in the bits above the type.
; CHECK: And(%0, 1152921504606846975:i64)

define i64 @f(i64 %x) {
  %m = lshr i64 -1, 4
  %r = and i64 %x, %m
  ret i64 %r
}
