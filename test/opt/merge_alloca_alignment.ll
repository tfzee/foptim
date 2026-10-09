; RUN: %foffcc %s --print-fir --passes "MergeAlloca" | FileCheck %s
; Regression test: MergeAlloca packed allocas back to back, so an 80 byte object
; after a 1 byte one ended up at offset 1 (misaligned this pointer, segfault).
; CHECK-LABEL: func f
; CHECK: PtrAdd(%{{[0-9]+}}, 16:
declare void @use(ptr, ptr)
define void @f() {
  %a = alloca i8, i32 1
  %b = alloca [80 x i8], align 8
  call void @use(ptr %a, ptr %b)
  ret void
}
