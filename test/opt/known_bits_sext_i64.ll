; RUN: %foffcc %s --print-fir --passes "SCCP,InstSimplify,DCE" | FileCheck %s
; Regression test: KnownBits shifted a u64 by 64 for a 64 bit SExt source and
; marked bits as both known 0 and known 1.
; CHECK: func f
define i64 @f(i32 %x) {
  %a = sext i32 4096 to i64
  %b = and i64 %a, 4096
  ret i64 %b
}
