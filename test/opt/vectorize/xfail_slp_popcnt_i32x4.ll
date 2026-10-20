; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering,Mem2Reg,InstSimplify,LVN,SCCP,DCE,SLPVectorizer,LegalizeVecs,InstSimplify,DCE" | FileCheck %s
; XFAIL: *
; The compiler aborts today: the SLP intrinsic recipe says ctpop is legal, but node_emit hits TODO("impl") for it (known-issues.md). Handwritten CHECK that describes the correct output, there is nothing to generate from while it crashes.
; CHECK: 4@i32 = INTRIN:PopCnt

declare i32 @llvm.ctpop.i32(i32)
define void @f(ptr %a, ptr %b) {
  %pb0 = getelementptr i32, ptr %b, i64 0
  %x0 = load i32, ptr %pb0
  %pb1 = getelementptr i32, ptr %b, i64 1
  %x1 = load i32, ptr %pb1
  %pb2 = getelementptr i32, ptr %b, i64 2
  %x2 = load i32, ptr %pb2
  %pb3 = getelementptr i32, ptr %b, i64 3
  %x3 = load i32, ptr %pb3
  %r0 = call i32 @llvm.ctpop.i32(i32 %x0)
  %r1 = call i32 @llvm.ctpop.i32(i32 %x1)
  %r2 = call i32 @llvm.ctpop.i32(i32 %x2)
  %r3 = call i32 @llvm.ctpop.i32(i32 %x3)
  %pa0 = getelementptr i32, ptr %a, i64 0
  store i32 %r0, ptr %pa0
  %pa1 = getelementptr i32, ptr %a, i64 1
  store i32 %r1, ptr %pa1
  %pa2 = getelementptr i32, ptr %a, i64 2
  store i32 %r2, ptr %pa2
  %pa3 = getelementptr i32, ptr %a, i64 3
  store i32 %r3, ptr %pa3
  ret void
}
