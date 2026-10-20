; RUN: %foffcc %s %t.o --cconffile %S/avx512.toml
; XFAIL: *
; With avx512f the loop vectorizer picks a vector factor of 16 (zmm), but the backend only encodes the zmm instructions of
; the copy / fill lowering: the vector fadd aborts in backend3_instr_gen.cpp (TODO UNREACH).

define void @f(ptr noalias %a, ptr noalias %b, ptr noalias %c, i64 %n) {
entry:
  %g = icmp sgt i64 %n, 0
  br i1 %g, label %loop, label %exit
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %pb = getelementptr float, ptr %b, i64 %i
  %x = load float, ptr %pb
  %pc = getelementptr float, ptr %c, i64 %i
  %y = load float, ptr %pc
  %r = fadd float %x, %y
  %pa = getelementptr float, ptr %a, i64 %i
  store float %r, ptr %pa
  %i1 = add nuw nsw i64 %i, 1
  %cnd = icmp slt i64 %i1, %n
  br i1 %cnd, label %loop, label %exit
exit:
  ret void
}
