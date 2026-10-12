; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; RUN: %foffcc %s %t.o

; vectorized reduction loop: 8 x i32 accumulator, horizontal add of an i32x8
define i32 @f(ptr %a, i64 %n) {
; CHECK-LABEL: func f
; CHECK: {{\$ymm[0-9]+}}: i32x8 = [{{.*}}]: i32x8
; CHECK: i32x8 + {{.*}}: i32x8
; CHECK: vextract128({{.*}}, 1: i8,
; CHECK: vpextr({{.*}}, 0: i8,
; CHECK: vpextr({{.*}}, 1: i8,
; CHECK: vpextr({{.*}}, 2: i8,
; CHECK: vpextr({{.*}}, 3: i8,
entry:
  %g = icmp sgt i64 %n, 0
  br i1 %g, label %loop, label %exit
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc1, %loop ]
  %p = getelementptr i32, ptr %a, i64 %i
  %x = load i32, ptr %p
  %acc1 = add i32 %acc, %x
  %i1 = add nuw nsw i64 %i, 1
  %cnd = icmp slt i64 %i1, %n
  br i1 %cnd, label %loop, label %exit
exit:
  %res = phi i32 [ 0, %entry ], [ %acc1, %loop ]
  ret i32 %res
}
