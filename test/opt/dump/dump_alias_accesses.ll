; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show="accesses,alias"}' --print-fir 2>&1 | FileCheck %s
; The decoded addresses and the alias answer of every pair of accesses (named
; by their index), and what the calls do to them. The alloca never escapes, so
; the call to @ext leaves it alone, while the plain argument %b is clobbered.
declare void @ext(ptr)
declare i32 @pure(ptr) readonly

define i32 @f(ptr noalias %a, ptr %b, i64 %i) {
; CHECK-LABEL: accesses of f
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] barrier_rw
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [3] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [4] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=8 + {{%[0-9]+}} * 4
; CHECK-NEXT: [5] barrier_r
; CHECK-LABEL: alias of f
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 0 3: No
; CHECK-NEXT: 0 4: No
; CHECK-NEXT: 2 3: No
; CHECK-NEXT: 2 4: No
; CHECK-NEXT: 3 4: No
; CHECK-NEXT: 1 -> 0: none
; CHECK-NEXT: 1 -> 2: none
; CHECK-NEXT: 1 -> 3: modref
; CHECK-NEXT: 1 -> 4: modref
; CHECK-NEXT: 5 -> 0: none
; CHECK-NEXT: 5 -> 2: none
; CHECK-NEXT: 5 -> 3: ref
; CHECK-NEXT: 5 -> 4: ref
  %l = alloca i32
  store i32 1, ptr %l
  call void @ext(ptr %b)
  %x = load i32, ptr %l
  %y = load i32, ptr %b
  %p = getelementptr inbounds i8, ptr %a, i64 8
  %q = getelementptr i32, ptr %p, i64 %i
  %z = load i32, ptr %q
  %c = call i32 @pure(ptr %b)
  ret i32 %x
}
