; RUN: %foffcc %s --print-fir --passes "SCCP" | FileCheck %s
; Pointers in a constant global are filled in by the linker, loading them can
; not be folded to the zeros in the initializer.

declare void @a()
declare void @b()
@table = private unnamed_addr constant [2 x ptr] [ptr @a, ptr @b]

define <2 x i64> @vec() {
; CHECK-LABEL: func vec
; CHECK: Load(G(table))
  %v = load <2 x i64>, ptr @table
  ret <2 x i64> %v
}

define i64 @scalar() {
; CHECK-LABEL: func scalar
; CHECK: Load(G(table))
  %v = load i64, ptr @table
  ret i64 %v
}
