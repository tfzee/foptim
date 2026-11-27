; Hand written CHECKs: the output is the PrintCostModel debug dump, which
; update_fir_tests.py does not know (same reason as test/opt/dump/dump_options.ll).
; RUN: %foffcc --mtune zen4 %s --passes 'PrintCostModel{show=["instrs","blocks"]}' --print-fir 2>&1 | FileCheck %s

; A 3 deep chain of ALU ops: the critical path (3 cycles) dominates the resource
; bound, so block estimate equals critpath.
define i32 @chain(i32 %a, i32 %b) {
; CHECK-LABEL: cost model instrs of chain
; CHECK-NEXT:    lat=1 tput=0.25 size=3
; CHECK-NEXT:    lat=1 tput=0.25 size=3
; CHECK-NEXT:    lat=1 tput=0.25 size=3
; CHECK-NEXT:    lat=0 tput=0 size=0
; CHECK-LABEL: cost model blocks of chain
; CHECK-NEXT:    bb0: resource=0.75 critpath=3 estimate=3
  %t1 = add i32 %a, %b
  %t2 = add i32 %t1, %b
  %t3 = add i32 %t2, %b
  ret i32 %t3
}
