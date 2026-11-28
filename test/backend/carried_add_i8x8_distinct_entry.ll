; The 64 bit carried vector of bytes starts from different scalars (vpinsrb into
; the partial type) and adds loaded bytes (no memory operand for a 64 bit
; vector). Only has to encode.
; RUN: %foffcc %s %t.o

define i8 @f(ptr %p, i64 %n, i8 %s0, i8 %s1, i8 %s2, i8 %s3, i8 %s4, i8 %s5, i8 %s6, i8 %s7) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %a0 = phi i8 [ %s0, %entry ], [ %m0, %loop ]
  %a1 = phi i8 [ %s1, %entry ], [ %m1, %loop ]
  %a2 = phi i8 [ %s2, %entry ], [ %m2, %loop ]
  %a3 = phi i8 [ %s3, %entry ], [ %m3, %loop ]
  %a4 = phi i8 [ %s4, %entry ], [ %m4, %loop ]
  %a5 = phi i8 [ %s5, %entry ], [ %m5, %loop ]
  %a6 = phi i8 [ %s6, %entry ], [ %m6, %loop ]
  %a7 = phi i8 [ %s7, %entry ], [ %m7, %loop ]
  %o = mul i64 %i, 8
  %q0 = getelementptr i8, ptr %p, i64 %o
  %o1 = add i64 %o, 1
  %q1 = getelementptr i8, ptr %p, i64 %o1
  %o2 = add i64 %o, 2
  %q2 = getelementptr i8, ptr %p, i64 %o2
  %o3 = add i64 %o, 3
  %q3 = getelementptr i8, ptr %p, i64 %o3
  %o4 = add i64 %o, 4
  %q4 = getelementptr i8, ptr %p, i64 %o4
  %o5 = add i64 %o, 5
  %q5 = getelementptr i8, ptr %p, i64 %o5
  %o6 = add i64 %o, 6
  %q6 = getelementptr i8, ptr %p, i64 %o6
  %o7 = add i64 %o, 7
  %q7 = getelementptr i8, ptr %p, i64 %o7
  %x0 = load i8, ptr %q0
  %x1 = load i8, ptr %q1
  %x2 = load i8, ptr %q2
  %x3 = load i8, ptr %q3
  %x4 = load i8, ptr %q4
  %x5 = load i8, ptr %q5
  %x6 = load i8, ptr %q6
  %x7 = load i8, ptr %q7
  %m0 = add i8 %a0, %x0
  %m1 = add i8 %a1, %x1
  %m2 = add i8 %a2, %x2
  %m3 = add i8 %a3, %x3
  %m4 = add i8 %a4, %x4
  %m5 = add i8 %a5, %x5
  %m6 = add i8 %a6, %x6
  %m7 = add i8 %a7, %x7
  %i.next = add i64 %i, 1
  %c = icmp ult i64 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  %t0 = add i8 %m0, %m1
  %t1 = add i8 %m2, %m3
  %t2 = add i8 %m4, %m5
  %t3 = add i8 %m6, %m7
  %u0 = add i8 %t0, %t1
  %u1 = add i8 %t2, %t3
  %r = add i8 %u0, %u1
  ret i8 %r
}
