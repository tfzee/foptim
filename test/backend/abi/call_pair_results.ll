; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; The caller reads the two results of a struct returning call from the return registers.

declare { i64, i64 } @pair_ll(i64)
declare { double, double } @pair_dd(double)
declare { i64, double } @pair_ld(i64)
declare { double, i64 } @pair_dl(i64)

; CHECK-LABEL: func use_ll
; CHECK: call(pair_ll, )
; CHECK: $rax: i64 -= $rdx: i64
define i64 @use_ll(i64 %x) {
  %r = call { i64, i64 } @pair_ll(i64 %x)
  %a = extractvalue { i64, i64 } %r, 0
  %b = extractvalue { i64, i64 } %r, 1
  %s = sub i64 %a, %b
  ret i64 %s
}

; CHECK-LABEL: func use_dd
; CHECK: call(pair_dd, )
; CHECK: $mm0: f64 = $mm0: f64 - $mm1: f64
define double @use_dd(double %x) {
  %r = call { double, double } @pair_dd(double %x)
  %a = extractvalue { double, double } %r, 0
  %b = extractvalue { double, double } %r, 1
  %s = fsub double %a, %b
  ret double %s
}

; CHECK-LABEL: func use_ld
; CHECK: call(pair_ld, )
; CHECK: SI2FL($mm1: f64, $rax: i64, )
; CHECK: $mm0: f64 = $mm1: f64 - $mm0: f64
define double @use_ld(i64 %x) {
  %r = call { i64, double } @pair_ld(i64 %x)
  %a = extractvalue { i64, double } %r, 0
  %b = extractvalue { i64, double } %r, 1
  %c = sitofp i64 %a to double
  %s = fsub double %c, %b
  ret double %s
}

; CHECK-LABEL: func use_dl
; CHECK: call(pair_dl, )
; CHECK: SI2FL($mm1: f64, $rax: i64, )
; CHECK: $mm0: f64 = $mm0: f64 - $mm1: f64
define double @use_dl(i64 %x) {
  %r = call { double, i64 } @pair_dl(i64 %x)
  %a = extractvalue { double, i64 } %r, 0
  %b = extractvalue { double, i64 } %r, 1
  %c = sitofp i64 %b to double
  %s = fsub double %a, %c
  ret double %s
}

; CHECK-LABEL: func forward_ll
; CHECK: call(pair_ll, )
; CHECK: ret($rax: i64, $rdx: i64, )
define { i64, i64 } @forward_ll(i64 %x) {
  %r = call { i64, i64 } @pair_ll(i64 %x)
  ret { i64, i64 } %r
}
