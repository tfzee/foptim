; Regression test: select on a float compare lowers to vcmp + vblendv. vblendv
; picks its second source where the mask is set, so the false value must be
; the first source operand (the true value 1.0 comes second).
; RUN: %foffcc %s --print-mir | FileCheck %s

define double @sel(double %a, double %b) {
; CHECK: func sel
; CHECK: $rax: i64 = infd
; CHECK: $mm1: f64 = $rax: i64
; CHECK: $rax: i64 = 1d
; CHECK: $mm2: f64 = $rax: i64
; CHECK: vblendv($mm0: f64, $mm1: f64, $mm2: f64, $xmm0: i64x2, )
  %c = fcmp olt double %a, %b
  %r = select i1 %c, double 1.0, double 0x7FF0000000000000
  ret double %r
}
