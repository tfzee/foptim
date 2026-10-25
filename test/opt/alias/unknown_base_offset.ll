; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show="accesses,alias"}' --print-fir 2>&1 | FileCheck %s

; AliasAnalyis must not treat "unknown base plus constant" as an offset into
; one shared object. %p and %q are loaded pointers, %p + 8 and
; %q + 16 are the same address when %q = %p - 8, so the second store may
; overwrite the first one, so stores 2 and 3 and the load 4 may alias (May).
define i64 @f(ptr %a, ptr %b) {
; CHECK-LABEL: accesses of f
; CHECK-NEXT: [0] load {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] load {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] store {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=8
; CHECK-NEXT: [3] store {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=16
; CHECK-NEXT: [4] load {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=8
; CHECK-LABEL: alias of f
; CHECK-NEXT: 0 1: May
; CHECK-NEXT: 0 2: May
; CHECK-NEXT: 0 3: May
; CHECK-NEXT: 0 4: May
; CHECK-NEXT: 1 2: May
; CHECK-NEXT: 1 3: May
; CHECK-NEXT: 1 4: May
; CHECK-NEXT: 2 3: May
; CHECK-NEXT: 2 4: Must
; CHECK-NEXT: 3 4: May
  %p = load ptr, ptr %a, align 8
  %q = load ptr, ptr %b, align 8
  %p8 = getelementptr i8, ptr %p, i64 8
  %q16 = getelementptr i8, ptr %q, i64 16
  store i64 1, ptr %p8, align 8
  store i64 2, ptr %q16, align 8
  %r = load i64, ptr %p8, align 8
  ret i64 %r
}
