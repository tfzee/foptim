; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; The argument moves of the first edge of a conditional branch ran in front of
; the jump, so they also ran on the exit path, where the exit argument still
; needed the old value of %n (this is __unguarded_linear_insert).
; CHECK-LABEL: func shift
; CHECK: $mm0: f64 = [$rax]: f64
; CHECK: cjmp_flt_olt({{.*}}) -> [[MOVES:[0-9]+]]
; CHECK: [[MOVES]]:
; CHECK-NEXT: $rdx: i64 = $rax: i64
; CHECK-NEXT: $rax: i64 = $rcx: i64
; CHECK-NEXT: jmp()
define void @shift(ptr %last, double %val) {
entry:
  %n0 = getelementptr inbounds double, ptr %last, i64 -1
  %x0 = load double, ptr %n0
  %c0 = fcmp olt double %val, %x0
  br i1 %c0, label %loop, label %exit
loop:
  %l = phi ptr [ %last, %entry ], [ %n, %loop ]
  %n = phi ptr [ %n0, %entry ], [ %n2, %loop ]
  %v = load double, ptr %n
  store double %v, ptr %l
  %n2 = getelementptr inbounds double, ptr %n, i64 -1
  %x = load double, ptr %n2
  %c = fcmp olt double %val, %x
  br i1 %c, label %loop, label %exit
exit:
  %e = phi ptr [ %last, %entry ], [ %n, %loop ]
  store double %val, ptr %e
  ret void
}
