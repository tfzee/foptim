; RUN: %foffcc %s --print-fir --passes "ConstLoopEval,InstSimplify,SimplifyCFG,DCE" | FileCheck %s
; ConstLoopEval can evaluate itrunc / lshr / negative zext inputs of small loops.

define i32 @shl_trunc() mustprogress {
; CHECK-LABEL: func shl_trunc<CC: C, LINK: external, > Uses: 0
; CHECK-NEXT:{
; CHECK-NEXT:  bb0():
; CHECK-NEXT:    %0: i32 = Return(255:i32){} NUses:0
; CHECK-NEXT:}
entry:
  br label %loop
loop:
  %acc = phi i32 [ 0, %entry ], [ %acc.n, %loop ]
  %i = phi i32 [ 7, %entry ], [ %i.n, %loop ]
  %iz = zext i32 %i to i64
  %s = shl i64 1, %iz
  %t = trunc i64 %s to i32
  %acc.n = add i32 %acc, %t
  %i.n = add nsw i32 %i, -1
  %c = icmp sge i32 %i.n, 0
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %acc.n
}

define i32 @lshr_loop() mustprogress {
; CHECK-LABEL: func lshr_loop<CC: C, LINK: external, > Uses: 0
; CHECK-NEXT:{
; CHECK-NEXT:  bb0():
; CHECK-NEXT:    %0: i32 = Return(255:i32){} NUses:0
; CHECK-NEXT:}
entry:
  br label %loop
loop:
  %acc = phi i32 [ 0, %entry ], [ %acc.n, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %t = lshr i32 128, %i
  %acc.n = add i32 %acc, %t
  %i.n = add i32 %i, 1
  %c = icmp slt i32 %i.n, 8
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %acc.n
}

; the i8 sum wraps: 100 * 3 = 300 -> 44
define i8 @wrap_add() mustprogress {
; CHECK-LABEL: func wrap_add<CC: C, LINK: external, > Uses: 0
; CHECK-NEXT:{
; CHECK-NEXT:  bb0():
; CHECK-NEXT:    %0: i8 = Return(44:i8){} NUses:0
; CHECK-NEXT:}
entry:
  br label %loop
loop:
  %acc = phi i8 [ 0, %entry ], [ %acc.n, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %acc.n = add i8 %acc, 100
  %i.n = add i32 %i, 1
  %c = icmp slt i32 %i.n, 3
  br i1 %c, label %loop, label %exit
exit:
  ret i8 %acc.n
}
