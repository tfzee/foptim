; RUN: %foffcc %s --print-fir --passes "Mem2Reg,InstSimplify,LVN,SCCP,DCE,SLPVectorizer,LegalizeVecs,InstSimplify,DCE" | FileCheck %s
; XFAIL: *
; The compiler aborts today: the SLP intrinsic recipe says signed compare + select (becomes smax) is legal, but node_emit hits TODO("impl") for it (known-issues.md). Handwritten CHECK that describes the correct output, there is nothing to generate from while it crashes.
; CHECK: 4@i32 = INTRIN:SMax

define void @f(ptr %a, ptr %b, ptr %c) {
  %a0 = getelementptr i32, ptr %a, i64 0
  %a1 = getelementptr i32, ptr %a, i64 1
  %a2 = getelementptr i32, ptr %a, i64 2
  %a3 = getelementptr i32, ptr %a, i64 3
  %b0 = getelementptr i32, ptr %b, i64 0
  %b1 = getelementptr i32, ptr %b, i64 1
  %b2 = getelementptr i32, ptr %b, i64 2
  %b3 = getelementptr i32, ptr %b, i64 3
  %c0 = getelementptr i32, ptr %c, i64 0
  %c1 = getelementptr i32, ptr %c, i64 1
  %c2 = getelementptr i32, ptr %c, i64 2
  %c3 = getelementptr i32, ptr %c, i64 3
  %x0 = load i32, ptr %b0
  %y0 = load i32, ptr %c0
  %x1 = load i32, ptr %b1
  %y1 = load i32, ptr %c1
  %x2 = load i32, ptr %b2
  %y2 = load i32, ptr %c2
  %x3 = load i32, ptr %b3
  %y3 = load i32, ptr %c3
  %cmp0 = icmp sgt i32 %x0, %y0
  %r0 = select i1 %cmp0, i32 %x0, i32 %y0
  %cmp1 = icmp sgt i32 %x1, %y1
  %r1 = select i1 %cmp1, i32 %x1, i32 %y1
  %cmp2 = icmp sgt i32 %x2, %y2
  %r2 = select i1 %cmp2, i32 %x2, i32 %y2
  %cmp3 = icmp sgt i32 %x3, %y3
  %r3 = select i1 %cmp3, i32 %x3, i32 %y3
  store i32 %r0, ptr %a0
  store i32 %r1, ptr %a1
  store i32 %r2, ptr %a2
  store i32 %r3, ptr %a3
  ret void
}
