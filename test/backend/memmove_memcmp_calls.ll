; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; memmove and memcmp the optimizer could not fold end up as libc calls, small
; constant sizes are plain loads and stores.
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)
declare i32 @memcmp(ptr, ptr, i64)

define void @small(ptr %d, ptr %s) {
; CHECK-LABEL: func small
; CHECK-NOT: call
; CHECK: ret()
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 12, i1 false)
  ret void
}

define void @big(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: func big
; CHECK: call(memmove, )
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  ret void
}

define i32 @dyn(ptr %a, ptr %b, i64 %n) {
; CHECK-LABEL: func dyn
; CHECK: call(memcmp, )
  %r = call i32 @memcmp(ptr %a, ptr %b, i64 %n)
  ret i32 %r
}

define i1 @eq7(ptr %a, ptr %b) {
; CHECK-LABEL: func eq7
; CHECK-NOT: call
; CHECK: ret(
  %r = call i32 @memcmp(ptr %a, ptr %b, i64 7)
  %c = icmp eq i32 %r, 0
  ret i1 %c
}
