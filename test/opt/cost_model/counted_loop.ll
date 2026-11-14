; Hand written CHECKs: the output is the PrintCostModel debug dump, which
; update_fir_tests.py does not know (same reason as test/opt/dump/dump_options.ll).
; RUN: %foffcc %s --passes 'PrintCostModel{show=["loops"]}' --print-fir 2>&1 | FileCheck %s

; A loop with a compile time constant trip count (10): LoopCostAnalysis
; should resolve trip=10 via TripCountAnalysis rather than falling back to
; the "unknown" assumed trip count (see uncounted_loop.ll for that case).
define i32 @counted() {
; CHECK-LABEL: cost model loops of counted
; CHECK-NEXT: loop head=bb1 trip=10 iter_resource=3 chain_latency=1 total=30
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %n, %loop ]
  %n = add i32 %i, 1
  %c = icmp slt i32 %n, 10
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %n
}
