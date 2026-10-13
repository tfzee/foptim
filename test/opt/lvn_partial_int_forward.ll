; Integer loads that are slices of a wider store, or tiled by narrower stores,
; are rebuilt from the stored values.
; RUN: %foffcc %s --print-fir --passes "InstSimplify,LVN,DCE" | FileCheck %s

define i32 @hi_of_wide(i64 %a) {
; CHECK-LABEL: func hi_of_wide
; CHECK: LShR(%0, 32:i64)
; CHECK: ITrunc
; CHECK-NOT: Load
  %p = alloca [8 x i8]
  store i64 %a, ptr %p
  %q = getelementptr i8, ptr %p, i64 4
  %v = load i32, ptr %q
  ret i32 %v
}

define i64 @tile(i32 %a, i32 %b) {
; CHECK-LABEL: func tile
; CHECK: ZExt
; CHECK: Shl
; CHECK: Or
; CHECK-NOT: Load
  %p = alloca [8 x i8]
  store i32 %a, ptr %p
  %q = getelementptr i8, ptr %p, i64 4
  store i32 %b, ptr %q
  %v = load i64, ptr %p
  ret i64 %v
}
