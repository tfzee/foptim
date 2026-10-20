; RUN: %foffcc %s %t.o
; XFAIL: *
; The backend cannot encode a vector floor yet: Zydis fails to encode vroundpd (backend3_instr_gen.cpp, TODO) and the compiler aborts.
; Reached by the SLP vectorizer too (test/opt/vectorize/slp_floor_f64x2.ll), it considers FFloor legal.

declare <2 x double> @llvm.floor.v2f64(<2 x double>)
define void @f(ptr %a, ptr %b) {
  %x = load <2 x double>, ptr %b
  %r = call <2 x double> @llvm.floor.v2f64(<2 x double> %x)
  store <2 x double> %r, ptr %a
  ret void
}
