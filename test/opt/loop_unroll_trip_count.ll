; RUN: %foffcc %s --print-fir --passes "LoopUnroll" | FileCheck %s
; The trip count (16) comes from TripCountAnalysis; the body is replicated.
; CHECK-COUNT-4: Store(

@g = global [64 x i32] zeroinitializer
define void @f(i32 %x) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %p = getelementptr i32, ptr @g, i32 %i
  store i32 %x, ptr %p
  %i.n = add i32 %i, 1
  %c = icmp slt i32 %i.n, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
