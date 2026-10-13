; llvm.memcpy and llvm.memset become the memcpy / memset intrinsics, the count
; is in elements of the intrinsic's element type (bytes here).
; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering" | FileCheck %s

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)

define void @f(ptr %d, ptr %s, i64 %n, i8 %c) {
; CHECK-LABEL: func f
; CHECK: INTRIN:MemCpy(%0, %1, %2){extTy: i8; }
; CHECK: INTRIN:MemSet(%0, %3, %2)
; CHECK-NOT: Call
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  call void @llvm.memset.p0.i64(ptr %d, i8 %c, i64 %n, i1 false)
  ret void
}
