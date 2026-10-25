; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show=["memssa"]}' --print-fir 2>&1 | FileCheck %s
; Calls that write are defs, readonly calls are uses. A non escaping alloca is
; not clobbered by the call, volatile accesses are defs and never looked past.
declare void @ext(ptr)
declare i32 @pure(ptr) readonly

define i32 @calls(ptr %a) {
; CHECK-LABEL: memory ssa of calls
; CHECK: ; [[S:[0-9]+]] = MemoryDef(0)  %{{[0-9]+}}: i32 = Store
; CHECK-NEXT: ; [[C:[0-9]+]] = MemoryDef([[S]])  %{{[0-9]+}}: () = Call(ext
; CHECK-NEXT: ; MemoryUse([[C]]) clobber=[[S]]
; CHECK-NEXT: ; MemoryUse([[C]]) clobber=[[C]]
; CHECK-NEXT: ; MemoryUse([[C]])  %{{[0-9]+}}: i32 = Call(pure
; CHECK-NEXT: ; {{[0-9]+}} = MemoryDef([[C]])  %{{[0-9]+}}: i32 = Load{{.*}}VOLATILE
  %l = alloca i32
  store i32 1, ptr %l
  call void @ext(ptr %a)
  %x = load i32, ptr %l
  %y = load i32, ptr %a
  %q = call i32 @pure(ptr %a)
  %z = load volatile i32, ptr %a
  ret i32 %x
}
