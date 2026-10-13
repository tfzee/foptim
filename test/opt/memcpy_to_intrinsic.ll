; llvm.memcpy and a libc memcpy become the memcpy intrinsic, the count is in
; elements of the intrinsic's element type (bytes here).
; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering,InstSimplify" | FileCheck %s

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare ptr @memcpy(ptr, ptr, i64)

define void @f(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: func f
; CHECK: INTRIN:MemCpy(%0, %1, %2){extTy: i8; }
; CHECK: INTRIN:MemCpy(%0, %1, 24:i64){extTy: i8; }
; CHECK-NOT: Call
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  %r = call ptr @memcpy(ptr %d, ptr %s, i64 24)
  ret void
}
