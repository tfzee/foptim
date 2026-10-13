; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering,IntrinLowerCalls" | FileCheck %s
; What is left of memmove / memcmp at the end becomes a libc call again.
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)
declare i32 @memcmp(ptr, ptr, i64)

define i32 @both(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: func both
; CHECK: Call(memmove, %0, %1, %2)
; CHECK: i32 = Call(memcmp, %0, %1, %2)
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  %r = call i32 @memcmp(ptr %d, ptr %s, i64 %n)
  ret i32 %r
}
