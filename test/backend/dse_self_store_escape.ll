; Guard tests for the alloca escape analysis used by DSE: a pointer to an
; alloca stored into the alloca itself must not make stores dead when the
; pointer read back out of it escapes.
; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s

%S = type { ptr, i64, [16 x i8] }
declare void @ext(ptr)

; the loaded self pointer is passed to a call
define void @neg_call() {
; CHECK: func neg_call ()
; CHECK: [$rax]: i64 = $rdi
; CHECK: call(ext, )
  %s = alloca %S, align 8
  %buf = getelementptr inbounds %S, ptr %s, i32 0, i32 2
  store ptr %buf, ptr %s, align 8
  %p = load ptr, ptr %s, align 8
  call void @ext(ptr %p)
  ret void
}

; the self pointer is read back as an integer and returned
define i64 @neg_int() {
; CHECK: $rsp: i64 -= 32: i64
; CHECK: $rax: i64 = $rsp: i64
; CHECK: $rax: i64 += 16: i64
; CHECK: $rsp: i64 += 32: i8
; CHECK: ret($rax: i64, )
  %s = alloca %S, align 8
  %buf = getelementptr inbounds %S, ptr %s, i32 0, i32 2
  store ptr %buf, ptr %s, align 8
  %i = load i64, ptr %s, align 8
  ret i64 %i
}
