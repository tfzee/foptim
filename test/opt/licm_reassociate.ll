; (x + a) + b with invariant a and b becomes x + (a + b), the sum is hoisted.
; RUN: %foffcc %s --print-fir --passes "LICM,DCE" | FileCheck %s

define i32 @f(ptr %p, i32 %a, i32 %b, i32 %n) {
; CHECK-LABEL: func f
; CHECK: IntAdd(%1, %2)
; CHECK: bb1
; CHECK-NOT: IntAdd(%{{[0-9]+}}, %2)
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i2, %loop]
  %s = phi i32 [0, %entry], [%s2, %loop]
  %x = load i32, ptr %p
  %t1 = add i32 %x, %a
  %t2 = add i32 %t1, %b
  %s2 = add i32 %s, %t2
  %i2 = add i32 %i, 1
  %c = icmp slt i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s2
}
