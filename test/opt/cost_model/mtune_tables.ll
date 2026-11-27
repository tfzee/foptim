; Hand written CHECKs: the output is the PrintCostModel debug dump, which
; update_fir_tests.py does not know (same reason as add_chain.ll).
; RUN: %foffcc %s --passes 'PrintCostModel{show=["instrs"]}' --print-fir --mtune lioncove 2>&1 | FileCheck %s --check-prefix=LION
; RUN: %foffcc %s --passes 'PrintCostModel{show=["instrs"]}' --print-fir --mtune zen4 2>&1 | FileCheck %s --check-prefix=ZEN
; RUN: %foffcc %s --passes 'PrintCostModel{show=["instrs"]}' --print-fir --mtune generic 2>&1 | FileCheck %s --check-prefix=GEN
; RUN: %foffcc %s --passes 'PrintCostModel{show=["instrs"]}' --print-fir 2>&1 | FileCheck %s --check-prefix=GEN
; RUN: not %foffcc %s --print-fir --mtune nosuchcpu 2>&1 | FileCheck %s --check-prefix=BAD

; The ALU throughput differs between the microarchitecture tables. No
; --mtune means the generic profile.
define i32 @f(i32 %a, i32 %b) {
; LION: lat=1 tput=0.18 size=3
; ZEN: lat=1 tput=0.25 size=3
; GEN: lat=1 tput=0.2 size=3
; BAD: unknown --mtune 'nosuchcpu', known: generic, zen4, goldencove, lioncove
  %t1 = add i32 %a, %b
  ret i32 %t1
}
