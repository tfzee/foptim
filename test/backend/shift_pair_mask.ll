; Regression test: (x >> n) << n and (x << n) >> n fold to masks of the right
; width (the mask was only right for n == bitwidth / 2).
; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s

; CHECK: func low (
; CHECK: $ecx: i32 = -16: i32
; CHECK-NEXT: land2($eax: i32, $ecx: i32, )
define i32 @low(i32 %x) {
  %a = lshr i32 %x, 4
  %b = shl i32 %a, 4
  ret i32 %b
}

; CHECK: func high (
; CHECK: land2($eax: i32, 268435455: i32, )
define i32 @high(i32 %x) {
  %a = shl i32 %x, 4
  %b = lshr i32 %a, 4
  ret i32 %b
}

; CHECK: func low64 (
; CHECK: $rcx: i64 = -16: i64
; CHECK-NEXT: land2($rax: i64, $rcx: i64, )
define i64 @low64(i64 %x) {
  %a = lshr i64 %x, 4
  %b = shl i64 %a, 4
  ret i64 %b
}
