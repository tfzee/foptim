; RUN: %foffcc %s --print-mir --no-reorder-funcs --cconffile %S/avx512.toml | FileCheck %s
; RUN: %foffcc %s %t.o --cconffile %S/avx512.toml
; With avx512f copies and fills use 64 byte zmm chunks.
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)

define void @copy100(ptr %d, ptr %s) {
; CHECK-LABEL: func copy100
; CHECK: $zmm0: i64x8 = [$rsi]: i64x8
; CHECK-NEXT: [$rdi]: i64x8 = $zmm0: i64x8
; CHECK-NEXT: $zmm0: i64x8 = [$rsi + 36]: i64x8
; CHECK-NEXT: [$rdi + 36]: i64x8 = $zmm0: i64x8
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 100, i1 false)
  ret void
}

define void @zero64(ptr %d) {
; CHECK-LABEL: func zero64
; CHECK: clear $zmm0: i64x8
; CHECK-NEXT: [$rdi]: i64x8 = $zmm0: i64x8
  call void @llvm.memset.p0.i64(ptr %d, i8 0, i64 64, i1 false)
  ret void
}
