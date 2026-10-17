; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering" | FileCheck %s
; The integer min / max intrinsics become the FIR intrinsics instead of staying calls.

declare i64 @llvm.smin.i64(i64, i64)
declare i64 @llvm.smax.i64(i64, i64)
declare i32 @llvm.umin.i32(i32, i32)
declare i32 @llvm.umax.i32(i32, i32)

; CHECK-LABEL: func f<
; CHECK-NOT: call
; CHECK: INTRIN:SMin
; CHECK: INTRIN:SMax
; CHECK: INTRIN:UMin
; CHECK: INTRIN:UMax
define i64 @f(i64 %a, i64 %b, i32 %c, i32 %d) {
  %1 = call i64 @llvm.smin.i64(i64 %a, i64 %b)
  %2 = call i64 @llvm.smax.i64(i64 %1, i64 %b)
  %3 = call i32 @llvm.umin.i32(i32 %c, i32 %d)
  %4 = call i32 @llvm.umax.i32(i32 %3, i32 %d)
  %5 = zext i32 %4 to i64
  %6 = add i64 %2, %5
  ret i64 %6
}
