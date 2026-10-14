; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering,InstSimplify,IntrinSimplify,DCE" | FileCheck %s
; Loads of bytes a big memset / memcpy wrote right before are folded, copies of
; copies read from the original.
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
declare void @use(ptr)

define i32 @load_after_zero_fill(ptr %p) {
; CHECK-LABEL: func load_after_zero_fill
; CHECK: INTRIN:MemSet
; CHECK-NOT: Load
; CHECK: Return(0:i32)
  call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 512, i1 false)
  %q = getelementptr i8, ptr %p, i64 100
  %v = load i32, ptr %q
  ret i32 %v
}

define i32 @load_after_fill_const(ptr %p) {
; CHECK-LABEL: func load_after_fill_const
; CHECK-NOT: Load
; CHECK: Return(1010580540:i32)
  call void @llvm.memset.p0.i64(ptr %p, i8 60, i64 512, i1 false)
  %q = getelementptr i8, ptr %p, i64 8
  %v = load i32, ptr %q
  ret i32 %v
}

define i16 @load_after_fill_var(ptr %p, i8 %c) {
; CHECK-LABEL: func load_after_fill_var
; CHECK-NOT: Load
; CHECK: IntMul
  call void @llvm.memset.p0.i64(ptr %p, i8 %c, i64 512, i1 false)
  %v = load i16, ptr %p
  ret i16 %v
}

define i32 @load_after_copy(ptr noalias %d, ptr %s) {
; CHECK-LABEL: func load_after_copy
; CHECK: INTRIN:MemCpy
; CHECK: PtrAdd(%1, 20:i64)
; CHECK-NEXT: i32 = Load
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 512, i1 false)
  %q = getelementptr i8, ptr %d, i64 20
  %v = load i32, ptr %q
  ret i32 %v
}

; a store into the source in between changes what the copy holds
define i32 @load_after_copy_src_written(ptr noalias %d, ptr %s) {
; CHECK-LABEL: func load_after_copy_src_written
; CHECK: INTRIN:MemCpy
; CHECK: Store
; CHECK: Load(%{{[0-9]+}})
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 512, i1 false)
  %sq = getelementptr i8, ptr %s, i64 20
  store i32 7, ptr %sq
  %q = getelementptr i8, ptr %d, i64 20
  %v = load i32, ptr %q
  ret i32 %v
}

; a call in between could write anything
define i32 @load_after_fill_call(ptr %p) {
; CHECK-LABEL: func load_after_fill_call
; CHECK: INTRIN:MemSet
; CHECK: Call
; CHECK: Load
  call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 512, i1 false)
  call void @use(ptr %p)
  %v = load i32, ptr %p
  ret i32 %v
}

define void @copy_chain(ptr noalias %a, ptr noalias %c) {
; CHECK-LABEL: func copy_chain
; CHECK: INTRIN:MemCpy(%{{[0-9]+}}, %0, 512:i64)
; CHECK: INTRIN:MemCpy(%1, %0, 512:i64)
  %b = alloca [512 x i8]
  call void @llvm.memcpy.p0.p0.i64(ptr %b, ptr %a, i64 512, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr %c, ptr %b, i64 512, i1 false)
  call void @use(ptr %b)
  ret void
}

define void @copy_of_fill(ptr noalias %c) {
; CHECK-LABEL: func copy_of_fill
; CHECK: INTRIN:MemSet
; CHECK: INTRIN:MemSet(%0, 9:i8, 512:i64)
  %b = alloca [512 x i8]
  call void @llvm.memset.p0.i64(ptr %b, i8 9, i64 512, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr %c, ptr %b, i64 512, i1 false)
  call void @use(ptr %b)
  ret void
}
