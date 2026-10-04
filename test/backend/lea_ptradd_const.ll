; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s

; (alloca + 128) + 16 must not be folded into a three operand lea that adds
; the base twice.

define ptr @f() {
; CHECK: func f ()
; CHECK-NOT: lea(
; CHECK: [$rbx + 16]: i64
  %a = alloca [160 x i8], align 8
  %p = getelementptr inbounds i8, ptr %a, i64 128
  %q = getelementptr inbounds i8, ptr %p, i64 16
  store i64 0, ptr %q
  ret ptr %q
}
