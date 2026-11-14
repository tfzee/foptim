; Hand written CHECKs: the output is the PrintCostModel debug dump, which
; update_fir_tests.py does not know (same reason as test/opt/dump/dump_options.ll).
; RUN: %foffcc %s --passes 'PrintCostModel{show=["instrs","blocks"]}' --print-fir 2>&1 | FileCheck %s

; The compare's result folds into the branch's own cmp+jcc encoding: the
; compare's latency is still real (lat=1 on the icmp line), but it
; contributes no separate uop/bytes of its own (tput=0 size=0 on that line),
; which is why bb0's resource bound below is 0.5 (one CMP_JCC_FUSED uop
; spread over 2 ports), not 1.0 (which it would be if the folded compare's
; uop were being double counted alongside the branch's).
define i32 @pick(i32 %a, i32 %b) {
; CHECK-LABEL: cost model instrs of pick
; CHECK-NEXT:    lat=1 tput=0 size=0
; CHECK-NEXT:    lat=1 tput=0.5 size=2
; CHECK-NEXT:    lat=0 tput=0 size=0
; CHECK-NEXT:    lat=0 tput=0 size=0
; CHECK-LABEL: cost model blocks of pick
; CHECK-NEXT:    bb0: resource=0.5 critpath=2 estimate=2
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 %a
f:
  ret i32 %b
}
