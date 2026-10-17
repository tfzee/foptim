; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; Two floating point results of a struct return go to xmm0 and xmm1, in struct order.

; CHECK-LABEL: func ret_dd
; CHECK: ret($mm0: f64, $mm1: f64, )
define { double, double } @ret_dd(double %a, double %b) {
  %x = insertvalue { double, double } poison, double %a, 0
  %y = insertvalue { double, double } %x, double %b, 1
  ret { double, double } %y
}

; CHECK-LABEL: func ret_dd_swapped
; CHECK: ret($mm0: f64, $mm1: f64, )
define { double, double } @ret_dd_swapped(double %a, double %b) {
  %x = insertvalue { double, double } poison, double %b, 0
  %y = insertvalue { double, double } %x, double %a, 1
  ret { double, double } %y
}

; CHECK-LABEL: func ret_ff
; CHECK: ret($mm0: f32, $mm1: f32, )
define { float, float } @ret_ff(float %a, float %b) {
  %x = insertvalue { float, float } poison, float %a, 0
  %y = insertvalue { float, float } %x, float %b, 1
  ret { float, float } %y
}
