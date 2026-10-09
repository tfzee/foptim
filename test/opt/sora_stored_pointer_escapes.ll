; RUN: %foffcc %s --print-fir --passes "InstSimplify,SORA" | FileCheck %s
; Regression test: SORA treated a store of the alloca pointer itself (not a store
; through it) as an access at offset 0 and replaced the stored pointer with the
; split off piece, so the escaped pointer pointed at the wrong object.
; CHECK-LABEL: func f
; CHECK: [[A:%[0-9]+]]: ptr = Alloca(16:i32)
; CHECK: Store(%{{[0-9]+}}, [[A]])
define void @f(ptr %out) {
  %a = alloca [16 x i8], align 8
  store i64 1, ptr %a
  %p = getelementptr i8, ptr %a, i64 8
  store i64 2, ptr %p
  store ptr %a, ptr %out
  ret void
}
