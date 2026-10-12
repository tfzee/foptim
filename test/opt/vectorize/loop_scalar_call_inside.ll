; RUN: %foffcc %s --print-fir --passes "Mem2Reg,SORA,InstSimplify,LVN,SCCP,SimplifyCFG,DCE,LICM,LoopRotate,SimplifyCFG,LoopSimplify,SLPVectorizer,LegalizeVecs,InstSimplify,DCE" | FileCheck %s
; Opaque call in the body: stays scalar.
; Handwritten CHECKs (declaration is printed as a second function).

declare void @ext(i32)

define void @f(ptr %a, i64 %n) {
entry:
  %g = icmp sgt i64 %n, 0
  br i1 %g, label %loop, label %exit
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]

  %p = getelementptr i32, ptr %a, i64 %i
  %x = load i32, ptr %p
  call void @ext(i32 %x)
  %i1 = add nuw nsw i64 %i, 1
  
  %cnd = icmp slt i64 %i1, %n
  br i1 %cnd, label %loop, label %exit
exit:

  ret void 
}

; CHECK-LABEL: func f<
; CHECK: Call(ext,
; CHECK-NOT: 4@i32
; CHECK-NOT: V.
