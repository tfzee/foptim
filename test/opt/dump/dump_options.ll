; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'DCE,PrintAnalysis{show=["accesses", "memssa"]},SimplifyCFG,PrintAnalysis{show="alias"}' --print-fir 2>&1 | FileCheck %s
; RUN: not %foffcc %s --passes 'PrintAnalysis{show=["nothing"]}' --print-fir 2>&1 | FileCheck %s --check-prefix=BADNAME
; RUN: not %foffcc %s --passes 'PrintAnalysis{shwo=["alias"]}' --print-fir 2>&1 | FileCheck %s --check-prefix=BADKEY
; The options of a pass go in a toml inline table behind its name. The commas
; inside the braces and brackets do not split the pass list, both forms of show
; (array and comma separated string) work, the dumps come in a fixed order (memssa, accesses, provenance, alias) and a bad name or key is an error.
define i32 @f(ptr %a, ptr %b) {
; CHECK-LABEL: memory ssa of f
; CHECK: MemoryDef(0)
; CHECK: MemoryUse
; CHECK-LABEL: accesses of f
; CHECK: store
; CHECK: load
; CHECK-LABEL: alias of f
; CHECK-NEXT: 0 1: May
; BADNAME: unknown analysis 'nothing'
; BADKEY: unknown option 'shwo'
  store i32 1, ptr %a
  %x = load i32, ptr %b
  ret i32 %x
}
