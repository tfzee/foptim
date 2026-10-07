; RUN: %foffcc %s %t.o
; constant as the first operand of a float compare used to reach the encoder as an immediate

declare void @a()
declare void @b()
define void @f(double %x) {
  %c = fcmp olt double 0.0, %x
  br i1 %c, label %t, label %e
t:
  call void @a()
  ret void
e:
  call void @b()
  ret void
}
