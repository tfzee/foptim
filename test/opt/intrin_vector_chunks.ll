; Copies and fills use 32 byte vector chunks and chunks that overlap when that
; saves operations.
; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering,IntrinSimplify,DCE" | FileCheck %s

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)

define void @copy48(ptr %d, ptr %s) {
; CHECK-LABEL: func copy48
; CHECK: 4@i64 = Load
; CHECK: 2@i64 = Load
; CHECK-NOT: i64 = Load
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 48, i1 false)
  ret void
}

define void @copy15(ptr %d, ptr %s) {
; CHECK-LABEL: func copy15
; CHECK: i64 = Load
; CHECK: PtrAdd({{.*}}, 7:i64)
; CHECK: i64 = Load
; CHECK-NOT: Load
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 15, i1 false)
  ret void
}

define void @copy15_volatile(ptr %d, ptr %s) {
; CHECK-LABEL: func copy15_volatile
; CHECK: i64 = Load
; CHECK: i32 = Load
; CHECK: i16 = Load
; CHECK: i8 = Load
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 15, i1 true)
  ret void
}

define void @zero64(ptr %d) {
; CHECK-LABEL: func zero64
; CHECK-NOT: INTRIN
; CHECK: 4@i64 = Store(%0, <0:i64
; CHECK: 4@i64 = Store({{.*}}, <0:i64
  call void @llvm.memset.p0.i64(ptr %d, i8 0, i64 64, i1 false)
  ret void
}

define void @fill_var50(ptr %d, i8 %c) {
; CHECK-LABEL: func fill_var50
; CHECK-NOT: INTRIN
; CHECK: IntMul
; CHECK: Broadcast
; CHECK: Store
; CHECK: Store
  call void @llvm.memset.p0.i64(ptr %d, i8 %c, i64 50, i1 false)
  ret void
}

define void @move40(ptr %d, ptr %s) {
; CHECK-LABEL: func move40
; CHECK: 4@i64 = Load
; CHECK: i64 = Load
; CHECK: Store
; CHECK: Store
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 40, i1 false)
  ret void
}
