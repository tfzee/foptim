; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s

; trunc to i1 must keep only bit 0, a plain byte test would branch on any set bit
define i32 @trunc_i1_branch(i64 %x) {
entry:
  %t = trunc i64 %x to i1
  br i1 %t, label %a, label %b
a:
  ret i32 1
b:
  ret i32 2
}
; CHECK-LABEL: func trunc_i1_branch
; CHECK: itrunc($dl: i8, $rdi: i64, )
; CHECK-NEXT: land2($dl: i8, 1: i8, )
