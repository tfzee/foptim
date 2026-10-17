; RUN: %foffcc %s --print-fir --passes "InstSimplify,LVN" | FileCheck %s
; A float load of bytes a vector of integers stored is the lane reinterpreted,
; the lane itself has the integer type.

define double @lane0(ptr %p, <4 x i64> %v) {
; CHECK-LABEL: func lane0
; CHECK: i64 = ExtractElement(%1, 0:i32)
; CHECK-NEXT: f64 = BITCAST
  store <4 x i64> %v, ptr %p
  %r = load double, ptr %p
  ret double %r
}

define double @lane2(ptr %p, <4 x i64> %v) {
; CHECK-LABEL: func lane2
; CHECK: i64 = ExtractElement(%1, 2:i32)
; CHECK-NEXT: f64 = BITCAST
  store <4 x i64> %v, ptr %p
  %q = getelementptr i8, ptr %p, i64 16
  %r = load double, ptr %q
  ret double %r
}
