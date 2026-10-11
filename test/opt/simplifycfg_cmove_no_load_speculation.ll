; RUN: %foffcc %s --print-fir --passes "SimplifyCFG" | FileCheck %s
; a load behind a null check must not be hoisted into a select

; CHECK-LABEL: func next_or_null
; CHECK-NOT: Select
define ptr @next_or_null(ptr %p) {
entry:
  %c = icmp ne ptr %p, null
  br i1 %c, label %yes, label %no
yes:
  %n = load ptr, ptr %p
  ret ptr %n
no:
  ret ptr null
}
