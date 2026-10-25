; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --cconffile %S/dump_pipeline.toml --print-fir 2>&1 | FileCheck %s
; RUN: %foffcc %s --passes '+PrintAnalysis{show="alias"}' --print-fir 2>&1 | FileCheck %s --check-prefix=APPEND
; The options of a pass work as an entry of a pipeline in the config file
; (an inline table behind the name inside the string) and with the + prefix of
; --passes, which appends to the configured pipeline.
define i32 @f(ptr %a, ptr %b) {
; CHECK-LABEL: accesses of f
; CHECK-NEXT: [0] store
; CHECK-NEXT: [1] load
; CHECK-NOT: alias of f
; APPEND-LABEL: alias of f
; APPEND-NEXT: 0 1: May
  store i32 1, ptr %a
  %x = load i32, ptr %b
  ret i32 %x
}
