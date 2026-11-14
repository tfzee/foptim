; Hand written CHECKs: the output is the PrintCostModel debug dump, which
; update_fir_tests.py does not know (same reason as test/opt/dump/dump_options.ll).
; RUN: %foffcc %s --passes 'PrintCostModel{show=["loops"]}' --print-fir 2>&1 | FileCheck %s

; The trip count depends on a function argument, so TripCountAnalysis has no
; compile time constant to report: LoopCostAnalysis falls back to its
; assumed trip count (100 by default), unlike counted_loop.ll's trip=10.
define i32 @uncounted(i32 %limit) {
; CHECK-LABEL: cost model loops of uncounted
; CHECK-NEXT: loop head=bb1 trip=unknown iter_resource=3 chain_latency=1 total=300
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %n, %loop ]
  %n = add i32 %i, 1
  %c = icmp slt i32 %n, %limit
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %n
}
