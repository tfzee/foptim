; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show=["provenance"]}' --print-fir 2>&1 | FileCheck %s
; Where the pointers of a function come from: an alloca that never escapes, one
; that does, a nonnull and a plain argument. Constants such as globals are not
; instructions and are not listed.
@g = global i32 0
declare void @take(ptr)

define void @f(ptr nonnull %nn, ptr %plain) {
; CHECK-LABEL: provenance of f
; CHECK-DAG: {{%[0-9]+}}: nonnull
; CHECK-DAG: {{%[0-9]+}}: maybe_null
; CHECK-DAG: {{%[0-9]+}}: stack nonnull
; CHECK-DAG: {{%[0-9]+}}: stack nonnull escapes
  %local = alloca i32
  %esc = alloca i32
  store i32 1, ptr %local
  call void @take(ptr %esc)
  store i32 2, ptr @g
  store i32 3, ptr %nn
  store i32 4, ptr %plain
  ret void
}
