; RUN: %foffcc %s --print-fir --passes "IntrinConv" | FileCheck %s
; a loop with step 2 must not become a memset of the whole range

; CHECK-LABEL: func fill_stride2
; CHECK-NOT: INTRIN:MemSet
define void @fill_stride2(ptr %p) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %a = getelementptr i8, ptr %p, i64 %i
  store i8 1, ptr %a
  %n = add i64 %i, 2
  %c = icmp ult i64 %n, 64
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
