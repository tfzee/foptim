; Hand written CHECKs: the output is the PrintCostModel debug dump, which
; update_fir_tests.py does not know (same reason as test/opt/dump/dump_options.ll).
; RUN: %foffcc %s --passes 'PrintCostModel{show=["instrs"]}' --print-fir 2>&1 | FileCheck %s

; The index multiply is a real IMUL, but the PtrAdd that folds into the
; load's own [base + idx] addressing mode costs nothing at all -- the load
; itself still costs a real LOAD, address math is what's free.
define i32 @load_at(ptr %p, i64 %i) {
; CHECK-LABEL: cost model instrs of load_at
; CHECK-NEXT:    lat=3 tput=1 size=3
; CHECK-NEXT:    lat=0 tput=0 size=0
; CHECK-NEXT:    lat=4 tput=0.33 size=4
; CHECK-NEXT:    lat=0 tput=0 size=0
  %addr = getelementptr i32, ptr %p, i64 %i
  %v = load i32, ptr %addr
  ret i32 %v
}
