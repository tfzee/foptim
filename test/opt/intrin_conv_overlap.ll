; RUN: %foffcc %s --print-fir --passes "IntrinConv" | FileCheck %s
; A copy loop only becomes a memcpy / memmove when its addresses advance by
; exactly one element and the overlap is known.

; disjoint ranges of one object
define void @disjoint(ptr %p) {
; CHECK-LABEL: func disjoint
; CHECK: INTRIN:MemCpy
; CHECK-NOT: INTRIN:MemMove
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %sa = getelementptr i32, ptr %p, i64 %i
  %da0 = getelementptr i32, ptr %p, i64 32
  %da = getelementptr i32, ptr %da0, i64 %i
  %v = load i32, ptr %sa
  store i32 %v, ptr %da
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; the destination is in front of the source, a forward loop is a memmove
define void @dst_first(ptr %p) {
; CHECK-LABEL: func dst_first
; CHECK: INTRIN:MemMove
; CHECK-NOT: INTRIN:MemCpy
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %s0 = getelementptr i32, ptr %p, i64 4
  %sa = getelementptr i32, ptr %s0, i64 %i
  %da = getelementptr i32, ptr %p, i64 %i
  %v = load i32, ptr %sa
  store i32 %v, ptr %da
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; the destination is behind the source and they overlap, the loop repeats the
; first elements which no memcpy / memmove does
define void @dst_last(ptr %p) {
; CHECK-LABEL: func dst_last
; CHECK-NOT: INTRIN
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %d0 = getelementptr i32, ptr %p, i64 4
  %da = getelementptr i32, ptr %d0, i64 %i
  %sa = getelementptr i32, ptr %p, i64 %i
  %v = load i32, ptr %sa
  store i32 %v, ptr %da
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; two pointers that may alias
define void @unknown(ptr %d, ptr %s) {
; CHECK-LABEL: func unknown
; CHECK-NOT: INTRIN
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %sa = getelementptr i32, ptr %s, i64 %i
  %da = getelementptr i32, ptr %d, i64 %i
  %v = load i32, ptr %sa
  store i32 %v, ptr %da
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; restrict makes them independent
define void @noalias(ptr noalias %d, ptr %s) {
; CHECK-LABEL: func noalias
; CHECK: INTRIN:MemCpy
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %sa = getelementptr i32, ptr %s, i64 %i
  %da = getelementptr i32, ptr %d, i64 %i
  %v = load i32, ptr %sa
  store i32 %v, ptr %da
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; every other element is not a contiguous copy
define void @gap(ptr noalias %d, ptr %s) {
; CHECK-LABEL: func gap
; CHECK-NOT: INTRIN
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %i2 = mul i64 %i, 2
  %sa = getelementptr i32, ptr %s, i64 %i2
  %da = getelementptr i32, ptr %d, i64 %i2
  %v = load i32, ptr %sa
  store i32 %v, ptr %da
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
