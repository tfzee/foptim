; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; An i128 argument or result is passed as two i64 registers, low limb first, like a { i64, i64 }.

; CHECK-LABEL: func add_wide
; CHECK: ret($rax: i64, $rdx: i64, )
define i128 @add_wide(i128 %a, i128 %b) noinline {
  %r = add i128 %a, %b
  ret i128 %r
}
