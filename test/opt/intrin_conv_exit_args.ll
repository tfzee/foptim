; RUN: %foffcc %s --print-fir --passes "IntrinConv,Mem2Reg" | FileCheck %s
; The converted loop is replaced by a branch to the exit block, which has to
; keep passing the block args of the old exit edge.
define void @f(ptr %p, ptr %q) {
; CHECK-LABEL: func f
; CHECK: MemSet
; CHECK: Branch<bb{{[0-9]+}}(0:i32)>
entry:
  br label %l1
l1:
  %i = phi i64 [ 0, %entry ], [ %n, %l1 ]
  %a = getelementptr inbounds i32, ptr %p, i64 %i
  store i32 3, ptr %a
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 100
  br i1 %c, label %l1, label %mid
mid:
  %j = phi i32 [ 0, %l1 ]
  br label %l2
l2:
  %k = phi i32 [ %j, %mid ], [ %k2, %l2 ]
  %k2 = add i32 %k, 1
  %c2 = icmp slt i32 %k2, 100
  br i1 %c2, label %l2, label %exit
exit:
  store i32 %k2, ptr %q
  ret void
}
