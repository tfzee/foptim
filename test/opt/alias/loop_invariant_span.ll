; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show="accesses,loop_alias"}' --print-fir 2>&1 | FileCheck %s
; XFAIL: *
;
; An across iterations question is answered by the base alone unless the query
; is given an IterSpan. With the span, an address whose base and index terms the
; loop does not move is compared by offsets again, which the jammed lanes of one
; accumulator need. across is the answer without a span, span the one with. The
; two lanes of a j jammed inner loop: same base %c, same index %i, 8 bytes
; apart. %i and the address math are outside the k loop, so only the span sees
; they cannot collide.
define void @jammed_lanes(ptr %c, i64 %i, i64 %n) {
; CHECK-LABEL: accesses of jammed_lanes
; CHECK-NEXT:    [0] load {{.*}} size=8 base=[[BASE:%[0-9]+]] offset=0 + [[IDX:%[0-9]+]] * 8
; CHECK-NEXT:    [1] store {{.*}} size=8 base=[[BASE]] offset=0 + [[IDX]] * 8
; CHECK-NEXT:    [2] load {{.*}} size=8 base=[[BASE]] offset=8 + [[IDX]] * 8
; CHECK-NEXT:    [3] store {{.*}} size=8 base=[[BASE]] offset=8 + [[IDX]] * 8
; CHECK-LABEL: loop alias of jammed_lanes
; CHECK-NEXT:    loop 0
; CHECK-NEXT:      0 1: across=May span=Must
; CHECK-NEXT:      0 2: across=May span=No
; CHECK-NEXT:      0 3: across=May span=No
; CHECK-NEXT:      1 2: across=May span=No
; CHECK-NEXT:      1 3: across=May span=No
; CHECK-NEXT:      2 3: across=May span=Must
entry:
  %c0 = getelementptr double, ptr %c, i64 %i
  %c1 = getelementptr double, ptr %c0, i64 1
  br label %loop
loop:
  %k = phi i64 [ 0, %entry ], [ %k1, %loop ]
  %v0 = load double, ptr %c0
  store double %v0, ptr %c0
  %v1 = load double, ptr %c1
  store double %v1, ptr %c1
  %k1 = add i64 %k, 1
  %cc = icmp slt i64 %k1, %n
  br i1 %cc, label %loop, label %exit
exit:
  ret void
}

; The index term is the induction variable, so the addresses move and the span
; changes nothing.
define void @variant_index(ptr %a, i64 %n) {
; CHECK-LABEL: accesses of variant_index
; CHECK-NEXT:    [0] load {{.*}} size=8 base=[[BASE:%[0-9]+]] offset=8 + [[IV:%[0-9]+]] * 8
; CHECK-NEXT:    [1] store {{.*}} size=8 base=[[BASE]] offset=0 + [[IV]] * 8
; CHECK-LABEL: loop alias of variant_index
; CHECK-NEXT:    loop 0
; CHECK-NEXT:      0 1: across=May span=May
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %p0 = getelementptr double, ptr %a, i64 %i
  %p1 = getelementptr double, ptr %p0, i64 1
  %v = load double, ptr %p1
  store double %v, ptr %p0
  %i1 = add i64 %i, 1
  %c = icmp slt i64 %i1, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; No index term, but the base is a pointer the loop bumps: the addresses share
; that base and are 8 bytes apart, yet the load of one iteration reads what the
; store of the next writes. Neither form may answer No.
define void @bumped_base(ptr %p, i64 %n) {
; CHECK-LABEL: accesses of bumped_base
; CHECK-NEXT:    [0] load {{.*}} size=8 base=[[Q:%[0-9]+]] offset=8
; CHECK-NEXT:    [1] store {{.*}} size=8 base=[[Q]] offset=0
; CHECK-LABEL: loop alias of bumped_base
; CHECK-NEXT:    loop 0
; CHECK-NEXT:      0 1: across=May span=May
entry:
  br label %loop
loop:
  %q = phi ptr [ %p, %entry ], [ %q1, %loop ]
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %src = getelementptr double, ptr %q, i64 1
  %v = load double, ptr %src
  store double %v, ptr %q
  %q1 = getelementptr double, ptr %q, i64 1
  %i1 = add i64 %i, 1
  %c = icmp slt i64 %i1, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
