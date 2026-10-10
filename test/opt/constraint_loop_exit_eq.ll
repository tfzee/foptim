; RUN: %foffcc %s --print-fir --passes "InstSimplify" | FileCheck %s
; The loop exits with %n == 8000 (a multiple of 16), the compare must stay.
; CHECK: IntEQ({{.*}}, 8000:i32)
define i32 @f() {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %n, %loop ]
  %n = add i32 %i, 16
  %c = icmp slt i32 %n, 8000
  br i1 %c, label %loop, label %exit
exit:
  %e = icmp eq i32 %n, 8000
  %r = zext i1 %e to i32
  ret i32 %r
}
