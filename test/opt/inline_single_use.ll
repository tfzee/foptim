; RUN: %foffcc %s --print-fir --passes "LateInline,GDCE" | FileCheck %s
; CHECK-NOT: callee
; CHECK: func caller
define internal i64 @callee(ptr %p, i64 %n) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i64 [ 0, %entry ], [ %acc.next, %loop ]
  %g = getelementptr i64, ptr %p, i64 %i
  %v = load i64, ptr %g
  %a1 = mul i64 %v, 3
  %a2 = add i64 %a1, %i
  %a3 = xor i64 %a2, %n
  %a4 = shl i64 %a3, 1
  %a5 = sub i64 %a4, %acc
  %a6 = and i64 %a5, 1023
  %a7 = or i64 %a6, %v
  %a8 = add i64 %a7, %i
  %b0 = add i64 %a8, 7
  %c0 = xor i64 %b0, %a6
  %b1 = add i64 %a8, 8
  %c1 = xor i64 %b1, %a6
  %b2 = add i64 %a8, 9
  %c2 = xor i64 %b2, %a6
  %b3 = add i64 %a8, 10
  %c3 = xor i64 %b3, %a6
  %b4 = add i64 %a8, 11
  %c4 = xor i64 %b4, %a6
  %b5 = add i64 %a8, 12
  %c5 = xor i64 %b5, %a6
  %b6 = add i64 %a8, 13
  %c6 = xor i64 %b6, %a6
  %b7 = add i64 %a8, 14
  %c7 = xor i64 %b7, %a6
  %b8 = add i64 %a8, 15
  %c8 = xor i64 %b8, %a6
  %b9 = add i64 %a8, 16
  %c9 = xor i64 %b9, %a6
  %b10 = add i64 %a8, 17
  %c10 = xor i64 %b10, %a6
  %b11 = add i64 %a8, 18
  %c11 = xor i64 %b11, %a6
  %b12 = add i64 %a8, 19
  %c12 = xor i64 %b12, %a6
  %b13 = add i64 %a8, 20
  %c13 = xor i64 %b13, %a6
  %b14 = add i64 %a8, 21
  %c14 = xor i64 %b14, %a6
  %b15 = add i64 %a8, 22
  %c15 = xor i64 %b15, %a6
  %b16 = add i64 %a8, 23
  %c16 = xor i64 %b16, %a6
  %b17 = add i64 %a8, 24
  %c17 = xor i64 %b17, %a6
  %b18 = add i64 %a8, 25
  %c18 = xor i64 %b18, %a6
  %b19 = add i64 %a8, 26
  %c19 = xor i64 %b19, %a6
  %acc.next = add i64 %acc, %c19
  store i64 %acc.next, ptr %g
  %i.next = add i64 %i, 1
  %c = icmp ult i64 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i64 %acc.next
}

define i64 @caller(ptr %p, i64 %n) {
  %r = call i64 @callee(ptr %p, i64 %n)
  ret i64 %r
}
