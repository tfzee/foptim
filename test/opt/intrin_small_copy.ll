; Copies and fills of at most 64 bytes with a constant count become plain
; loads and stores.
; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering,IntrinSimplify" | FileCheck %s

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)

define void @copy24(ptr %d, ptr %s) {
; CHECK-LABEL: func copy24
; CHECK-NOT: INTRIN
; CHECK: Load
; CHECK: Store
; CHECK: Load
; CHECK: Store
; CHECK: Load
; CHECK: Store
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 24, i1 false)
  ret void
}

define void @fill12(ptr %d) {
; CHECK-LABEL: func fill12
; CHECK-NOT: INTRIN
; CHECK: Store(%0, 506381209866536711:i64)
; CHECK: Store({{.*}}, 117901063:i32)
  call void @llvm.memset.p0.i64(ptr %d, i8 7, i64 12, i1 false)
  ret void
}

define void @fill_var(ptr %d, i8 %c) {
; CHECK-LABEL: func fill_var
; CHECK-NOT: INTRIN
; CHECK: IntMul
  call void @llvm.memset.p0.i64(ptr %d, i8 %c, i64 4, i1 false)
  ret void
}

define void @big(ptr %d, ptr %s) {
; CHECK-LABEL: func big
; CHECK: INTRIN:MemCpy
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 65, i1 false)
  ret void
}
