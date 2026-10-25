; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show="accesses,alias"}' --print-fir 2>&1 | FileCheck %s
;
; No metadata, only the kind of the base pointers. Each function stores
; through one pointer, then another, and loads the first again. The dump shows
; the answer for the second store and the load: No when the two are provably
; apart.
@g1 = global i32 0
@g2 = global i32 0
@c1 = constant i32 5
declare ptr @get()
declare void @sink(ptr)

define i32 @two_plain_args(ptr %a, ptr %b) {
; CHECK-LABEL: accesses of two_plain_args
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of two_plain_args
; CHECK-NEXT: 0 1: May
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: May
  store i32 1, ptr %a, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}

define i32 @two_noalias_args(ptr noalias %a, ptr noalias %b) {
; CHECK-LABEL: accesses of two_noalias_args
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of two_noalias_args
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  store i32 1, ptr %a, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}

define i32 @noalias_and_plain(ptr noalias %a, ptr %b) {
; CHECK-LABEL: accesses of noalias_and_plain
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of noalias_and_plain
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  store i32 1, ptr %a, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}

define i32 @two_globals() {
; CHECK-LABEL: accesses of two_globals
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base=G(g1) offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base=G(g2) offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base=G(g1) offset=0
; CHECK-LABEL: alias of two_globals
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  store i32 1, ptr @g1, align 4
  store i32 2, ptr @g2, align 4
  %r = load i32, ptr @g1, align 4
  ret i32 %r
}

define i32 @global_and_arg(ptr %b) {
; CHECK-LABEL: accesses of global_and_arg
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base=G(g1) offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base=G(g1) offset=0
; CHECK-LABEL: alias of global_and_arg
; CHECK-NEXT: 0 1: May
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: May
  store i32 1, ptr @g1, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr @g1, align 4
  ret i32 %r
}

define i32 @noalias_arg_and_global(ptr noalias %a) {
; CHECK-LABEL: accesses of noalias_arg_and_global
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base=G(g1) offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of noalias_arg_and_global
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  store i32 1, ptr %a, align 4
  store i32 2, ptr @g1, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}

; a local object that never escapes cannot be an argument
define i32 @local_and_arg(ptr %b) {
; CHECK-LABEL: accesses of local_and_arg
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of local_and_arg
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  %l = alloca i32, align 4
  store i32 1, ptr %l, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %l, align 4
  ret i32 %r
}

define i32 @escaped_local_and_call(ptr %b) {
; CHECK-LABEL: accesses of escaped_local_and_call
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] barrier_rw {{%[0-9]+}}
; CHECK-NEXT: [2] barrier_rw {{%[0-9]+}}
; CHECK-NEXT: [3] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [4] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of escaped_local_and_call
; CHECK-NEXT: 0 3: May
; CHECK-NEXT: 0 4: Must
; CHECK-NEXT: 3 4: May
; CHECK-NEXT: 1 -> 0: modref
; CHECK-NEXT: 1 -> 3: modref
; CHECK-NEXT: 1 -> 4: modref
; CHECK-NEXT: 2 -> 0: modref
; CHECK-NEXT: 2 -> 3: modref
; CHECK-NEXT: 2 -> 4: modref
  %l = alloca i32, align 4
  store i32 1, ptr %l, align 4
  call void @sink(ptr %l)
  %o = call ptr @get()
  store i32 2, ptr %o, align 4
  %r = load i32, ptr %l, align 4
  ret i32 %r
}

; two different locals
define i32 @two_locals() {
; CHECK-LABEL: accesses of two_locals
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of two_locals
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  %l1 = alloca i32, align 4
  %l2 = alloca i32, align 4
  store i32 1, ptr %l1, align 4
  store i32 2, ptr %l2, align 4
  %r = load i32, ptr %l1, align 4
  ret i32 %r
}

define i32 @select_of_locals(ptr %b, i1 %c) {
; CHECK-LABEL: accesses of select_of_locals
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of select_of_locals
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  %l1 = alloca i32, align 4
  %l2 = alloca i32, align 4
  %p = select i1 %c, ptr %l1, ptr %l2
  store i32 1, ptr %p, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %p, align 4
  ret i32 %r
}

define i32 @select_local_and_arg(ptr %a, ptr %b, i1 %c) {
; CHECK-LABEL: accesses of select_local_and_arg
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of select_local_and_arg
; CHECK-NEXT: 0 1: May
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: May
  %l1 = alloca i32, align 4
  %p = select i1 %c, ptr %l1, ptr %a
  store i32 1, ptr %p, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %p, align 4
  ret i32 %r
}

; a pointer plus an offset stays in its object, different offsets of one base
define i32 @same_base_offsets(ptr %a) {
; CHECK-LABEL: accesses of same_base_offsets
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=4
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of same_base_offsets
; CHECK-NEXT: 0 1: No
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: No
  %p4 = getelementptr i8, ptr %a, i64 4
  store i32 1, ptr %a, align 4
  store i32 2, ptr %p4, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}

define i32 @same_base_overlap(ptr %a) {
; CHECK-LABEL: accesses of same_base_overlap
; CHECK-NEXT: [0] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=2
; CHECK-NEXT: [2] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of same_base_overlap
; CHECK-NEXT: 0 1: Partial
; CHECK-NEXT: 0 2: Must
; CHECK-NEXT: 1 2: Partial
  %p2 = getelementptr i8, ptr %a, i64 2
  store i32 1, ptr %a, align 4
  store i32 2, ptr %p2, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}

; two pointers loaded from memory are unknown
define i32 @two_loaded_pointers(ptr %pa, ptr %pb) {
; CHECK-LABEL: accesses of two_loaded_pointers
; CHECK-NEXT: [0] load {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] load {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [3] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [4] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of two_loaded_pointers
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
  %a = load ptr, ptr %pa, align 8
  %b = load ptr, ptr %pb, align 8
  store i32 1, ptr %a, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %a, align 4
  ret i32 %r
}

; a local that stores its own address: the loaded pointer is the local itself or
; what someone else stored there (Mem2Reg is not run, so the load stays),
; and an argument still cannot point into a local that never escapes
define i32 @local_with_self_pointer(ptr %b) {
; CHECK-LABEL: accesses of local_with_self_pointer
; CHECK-NEXT: [0] store {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [1] load {{%[0-9]+}} size=8 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [2] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [3] store {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-NEXT: [4] load {{%[0-9]+}} size=4 base={{%[0-9]+}} offset=0
; CHECK-LABEL: alias of local_with_self_pointer
; CHECK-NEXT: 0 1: Must
; CHECK-NEXT: 0 2: May
; CHECK-NEXT: 0 3: No
; CHECK-NEXT: 0 4: May
; CHECK-NEXT: 1 2: May
; CHECK-NEXT: 1 3: No
; CHECK-NEXT: 1 4: May
; CHECK-NEXT: 2 3: May
; CHECK-NEXT: 2 4: Must
; CHECK-NEXT: 3 4: May
  %l = alloca ptr, align 8
  store ptr %l, ptr %l, align 8
  %p = load ptr, ptr %l, align 8
  store i32 1, ptr %p, align 4
  store i32 2, ptr %b, align 4
  %r = load i32, ptr %p, align 4
  ret i32 %r
}
