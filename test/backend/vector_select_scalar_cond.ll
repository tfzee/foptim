; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; vector select with a scalar float condition used to hit a TODO in the matcher

; CHECK-LABEL: func sel
; CHECK: mov_zx
; CHECK: neg1
; CHECK: vbroadcast
; CHECK: vblendv
define <4 x i64> @sel(float %x, float %y, <4 x i64> %a, <4 x i64> %b) {
  %c = fcmp olt float %x, %y
  %r = select i1 %c, <4 x i64> %a, <4 x i64> %b
  ret <4 x i64> %r
}
