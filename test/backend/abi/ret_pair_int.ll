; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; Two integer results of a struct return go to rax and rdx, in struct order.

; CHECK-LABEL: func ret_ll
; CHECK: ret($rax: i64, $rdx: i64, )
define { i64, i64 } @ret_ll(i64 %a, i64 %b) {
  %x = insertvalue { i64, i64 } poison, i64 %a, 0
  %y = insertvalue { i64, i64 } %x, i64 %b, 1
  ret { i64, i64 } %y
}

; CHECK-LABEL: func ret_ll_swapped
; CHECK: $rax: i64 = $rsi: i64
; CHECK: $rdx: i64 = $rdi: i64
; CHECK: ret($rax: i64, $rdx: i64, )
define { i64, i64 } @ret_ll_swapped(i64 %a, i64 %b) {
  %x = insertvalue { i64, i64 } poison, i64 %b, 0
  %y = insertvalue { i64, i64 } %x, i64 %a, 1
  ret { i64, i64 } %y
}

; CHECK-LABEL: func ret_pn
; CHECK: ret($rax: i64, $rdx: i64, )
define { ptr, i64 } @ret_pn(ptr %p, i64 %n) {
  %x = insertvalue { ptr, i64 } poison, ptr %p, 0
  %y = insertvalue { ptr, i64 } %x, i64 %n, 1
  ret { ptr, i64 } %y
}

; CHECK-LABEL: func ret_ii
; CHECK: ret($eax: i32, $edx: i32, )
define { i32, i32 } @ret_ii(i32 %a, i32 %b) {
  %x = insertvalue { i32, i32 } poison, i32 %a, 0
  %y = insertvalue { i32, i32 } %x, i32 %b, 1
  ret { i32, i32 } %y
}
