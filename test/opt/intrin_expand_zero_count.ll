; The expanded loops are do while loops, so a count that can be 0 gets a skip.
; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering,IntrinExpand" | FileCheck %s

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

define void @f(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: func f
; CHECK: IntNE(%2, 0:i64)
; CHECK: CondBranch
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  ret void
}
