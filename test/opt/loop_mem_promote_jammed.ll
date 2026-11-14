; RUN: %foffcc %s --print-fir --passes "Mem2Reg,LoopRotate,LoopSimplify,DCE" | FileCheck %s
;
; Unroll and jam turns one accumulator into several lanes whose addresses share
; a base and differ by a constant. Promoting them needs the across iterations
; alias question asked with the IterSpan of the loop, else every lane store
; blocks every other lane load. No noalias needed, the lanes share their base.

define void @jammed_acc(ptr nocapture %c, ptr nocapture %a, i64 %i, i64 %n) {
entry:
  %c0 = getelementptr double, ptr %c, i64 %i
  %c1 = getelementptr double, ptr %c0, i64 1
  br label %loop
loop:
  %k = phi i64 [ 0, %entry ], [ %k1, %loop ]
  %ap = getelementptr double, ptr %a, i64 %k
  %x = load double, ptr %ap
  %s0 = load double, ptr %c0
  %n0 = fadd double %s0, %x
  store double %n0, ptr %c0
  %s1 = load double, ptr %c1
  %n1 = fadd double %s1, %x
  store double %n1, ptr %c1
  %k1 = add i64 %k, 1
  %cc = icmp slt i64 %k1, %n
  br i1 %cc, label %loop, label %exit
exit:
  ret void
}

; Both lanes are loaded once in the preheader and carried as loop arguments,
; only the load of %a is left in the body. The stores stay.
; CHECK-LABEL: func jammed_acc<CC: C, LINK: external, > Uses: 0
; CHECK:         [[C0:%[0-9]+]]: ptr = PtrAdd(%0, {{%[0-9]+}}){}
; CHECK:         [[C1:%[0-9]+]]: ptr = PtrAdd([[C0]], {{%[0-9]+}}){}
; CHECK-NEXT:    [[I0:%[0-9]+]]: f64 = Load([[C0]]){}
; CHECK-NEXT:    [[I1:%[0-9]+]]: f64 = Load([[C1]]){}
; CHECK-NEXT:    {{%[0-9]+}}: () = Branch<bb1(0:i64, [[I0]], [[I1]])>(){}
; CHECK-NEXT:  bb1({{%[0-9]+}}{}: i64, [[A0:%[0-9]+]]{}: f64, [[A1:%[0-9]+]]{}: f64):
; CHECK:         [[X:%[0-9]+]]: f64 = Load({{%[0-9]+}}){}
; CHECK-NEXT:    [[S0:%[0-9]+]]: f64 = FloatAdd([[A0]], [[X]]){}
; CHECK-NEXT:    {{%[0-9]+}}: f64 = Store([[C0]], [[S0]]){}
; CHECK-NEXT:    [[S1:%[0-9]+]]: f64 = FloatAdd([[A1]], [[X]]){}
; CHECK-NEXT:    {{%[0-9]+}}: f64 = Store([[C1]], [[S1]]){}
; CHECK-NOT:     Load([[C0]])
; CHECK-NOT:     Load([[C1]])

; The conservative case the span does not touch: two accumulators under
; unrelated pointer arguments. Nothing says they are apart, so each store may
; be the other's load and neither is promoted.
define void @two_bases(ptr nocapture %p, ptr nocapture %q, ptr nocapture %a,
                       i64 %n) {
entry:
  br label %loop
loop:
  %k = phi i64 [ 0, %entry ], [ %k1, %loop ]
  %ap = getelementptr double, ptr %a, i64 %k
  %x = load double, ptr %ap
  %s0 = load double, ptr %p
  %n0 = fadd double %s0, %x
  store double %n0, ptr %p
  %s1 = load double, ptr %q
  %n1 = fadd double %s1, %x
  store double %n1, ptr %q
  %k1 = add i64 %k, 1
  %cc = icmp slt i64 %k1, %n
  br i1 %cc, label %loop, label %exit
exit:
  ret void
}

; Both loads stay inside the body, the header takes no accumulator argument.
; CHECK-LABEL: func two_bases<CC: C, LINK: external, > Uses: 0
; CHECK:       bb1({{%[0-9]+}}{}: i64):
; CHECK:         {{%[0-9]+}}: f64 = Load(%0){}
; CHECK:         {{%[0-9]+}}: f64 = Load(%1){}
