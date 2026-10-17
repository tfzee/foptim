; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; A struct return with one integer and one floating point member uses the next free register of each
; class: the integer one is rax and the floating point one xmm0, whatever their order in the struct.

; CHECK-LABEL: func ret_ld
; CHECK: ret($rax: i64, $mm0: f64, )
define { i64, double } @ret_ld(i64 %a, double %b) {
  %x = insertvalue { i64, double } poison, i64 %a, 0
  %y = insertvalue { i64, double } %x, double %b, 1
  ret { i64, double } %y
}

; CHECK-LABEL: func ret_dl
; CHECK: ret($mm0: f64, $rax: i64, )
define { double, i64 } @ret_dl(double %a, i64 %b) {
  %x = insertvalue { double, i64 } poison, double %a, 0
  %y = insertvalue { double, i64 } %x, i64 %b, 1
  ret { double, i64 } %y
}

; CHECK-LABEL: func ret_pd
; CHECK: ret($rax: i64, $mm0: f64, )
define { ptr, double } @ret_pd(ptr %a, double %b) {
  %x = insertvalue { ptr, double } poison, ptr %a, 0
  %y = insertvalue { ptr, double } %x, double %b, 1
  ret { ptr, double } %y
}
