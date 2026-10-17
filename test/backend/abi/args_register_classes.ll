; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; Integer and floating point arguments are assigned to their register files independently and in order,
; the 7th integer and the 9th floating point argument go to the stack.

; CHECK-LABEL: func int6
; CHECK: $rax: i64 = $rdi: i64
; CHECK: $rax: i64 += $rsi: i64
; CHECK: $rax: i64 += $rdx: i64
; CHECK: $rax: i64 += $rcx: i64
; CHECK: $rax: i64 += $r8: i64
; CHECK: $rax: i64 += $r9: i64
define i64 @int6(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f) {
  %1 = add i64 %a, %b
  %2 = add i64 %1, %c
  %3 = add i64 %2, %d
  %4 = add i64 %3, %e
  %5 = add i64 %4, %f
  ret i64 %5
}

; CHECK-LABEL: func mixed
; CHECK: SI2FL($mm1: f64, $rdi: i64, )
; CHECK: SI2FL($mm2: f64, $rsi: i64, )
define double @mixed(i64 %a, double %b, i64 %c, double %d) {
  %x = sitofp i64 %a to double
  %y = sitofp i64 %c to double
  %1 = fadd double %x, %b
  %2 = fadd double %1, %y
  %3 = fadd double %2, %d
  ret double %3
}

; CHECK-LABEL: func int7
; CHECK: $rax: i64 = [$rbp + 16]: i64
; CHECK: $rax: i64 -= $rdi: i64
define i64 @int7(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g) {
  %1 = sub i64 %g, %a
  ret i64 %1
}

; CHECK-LABEL: func sse9
; CHECK: $mm8: f64 = [$rbp + 16]: f64
; CHECK: $mm0: f64 = $mm8: f64 - $mm0: f64
define double @sse9(double %a, double %b, double %c, double %d, double %e, double %f, double %g, double %h, double %i) {
  %1 = fsub double %i, %a
  ret double %1
}

; CHECK-LABEL: func int_after_sse_stack
; CHECK: $rax: i64 = $rdi: i64
; CHECK: ret($rax: i64, )
define i64 @int_after_sse_stack(double %a, double %b, double %c, double %d, double %e, double %f, double %g, double %h, double %i, i64 %j) {
  ret i64 %j
}
