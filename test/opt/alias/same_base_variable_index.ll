; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show="accesses,alias"}' --print-fir 2>&1 | FileCheck %s

; p[i] and p[i + 1] have the same base and the same variable part, so their
; offsets decide: the store to p[i + 1] does not alias the load of p[i] (No).
; The store to q[i] may be the same address as p[i] (May).
define i32 @same_base(ptr %p, i64 %i) {
; CHECK-LABEL: accesses of same_base
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0 + {{%[0-9]+}} * 4
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=4 + {{%[0-9]+}} * 4
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0 + {{%[0-9]+}} * 4
; CHECK-LABEL: alias of same_base
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  %a = getelementptr i32, ptr %p, i64 %i
  %j = add i64 %i, 1
  %b = getelementptr i32, ptr %p, i64 %j
  store i32 1, ptr %a, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}

define i32 @other_base(ptr %p, ptr %q, i64 %i) {
; CHECK-LABEL: accesses of other_base
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0 + {{%[0-9]+}} * 4
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0 + {{%[0-9]+}} * 4
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0 + {{%[0-9]+}} * 4
; CHECK-LABEL: alias of other_base
; CHECK-NEXT: 0 1: May
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: May
  %a = getelementptr i32, ptr %p, i64 %i
  %b = getelementptr i32, ptr %q, i64 %i
  store i32 1, ptr %a, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}
