; RUN: %foffcc %s --print-fir --passes "InstSimplify,DCE" | FileCheck %s

; Shift pairs with vector shift amounts do not crash the shift merging.
; CHECK-LABEL: func f
define <4 x i64> @f(<4 x i64> %x) {
  %a = shl <4 x i64> %x, <i64 3, i64 3, i64 3, i64 3>
  %b = lshr <4 x i64> %a, <i64 3, i64 3, i64 3, i64 3>
  %c = lshr <4 x i64> %b, <i64 1, i64 1, i64 1, i64 1>
  %d = shl <4 x i64> %c, <i64 1, i64 1, i64 1, i64 1>
  ret <4 x i64> %d
}
