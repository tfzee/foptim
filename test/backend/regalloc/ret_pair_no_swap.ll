; Regression test: the destination of the first ret_setup of a two register return
; must not be assigned the register pinned by a later ret_setup of the same chunk
; (rdx), otherwise `mov rdx, rax; mov rax, rdx` loses the second return value.
; RUN: %foffcc %s %t.o --no-reorder-funcs
; RUN: llvm-objdump -d --no-show-raw-insn -M intel %t.o | FileCheck %s
; CHECK-LABEL: <emplace>:
; CHECK-NOT: mov rdx, rax
; CHECK: ret

%"struct.std::_Rb_tree_iterator" = type { ptr }
%"struct.std::_Rb_tree<double, std::pair<const double, double>, std::_Select1st<std::pair<const double, double>>, std::less<double>>::_Auto_node" = type { ptr, ptr }
%"struct.std::pair.2" = type { ptr, ptr }

$emplace = comdat any

declare i32 @__gxx_personality_v0(...)

; Function Attrs: mustprogress noinline optnone sspstrong uwtable
define weak_odr ptr @emplace(ptr noundef nonnull align 8 dereferenceable(48) %0, ptr noundef nonnull align 8 dereferenceable(16) %1) #0 comdat align 2 personality ptr @__gxx_personality_v0 {
  %3 = alloca %"struct.std::_Rb_tree_iterator", align 8
  %4 = alloca ptr, align 8
  %5 = alloca ptr, align 8
  %6 = alloca %"struct.std::_Rb_tree<double, std::pair<const double, double>, std::_Select1st<std::pair<const double, double>>, std::less<double>>::_Auto_node", align 8
  %7 = alloca %"struct.std::pair.2", align 8
  %8 = alloca ptr, align 8
  %9 = alloca i32, align 4
  %10 = alloca %"struct.std::pair.2", align 8
  store ptr %0, ptr %4, align 8
  store ptr %1, ptr %5, align 8
  %11 = load ptr, ptr %4, align 8
  %12 = load ptr, ptr %5, align 8
  call void @node_init(ptr noundef nonnull align 8 dereferenceable(16) %6, ptr noundef nonnull align 8 dereferenceable(48) %11, ptr noundef nonnull align 8 dereferenceable(16) %12)
  %13 = invoke noundef nonnull align 8 dereferenceable(8) ptr @node_key(ptr noundef nonnull align 8 dereferenceable(16) %6)
          to label %14 unwind label %30

14:                                               ; preds = %2
  %15 = invoke { ptr, ptr } @insert_pos(ptr noundef nonnull align 8 dereferenceable(48) %11, ptr noundef nonnull align 8 dereferenceable(8) %13)
          to label %16 unwind label %30

16:                                               ; preds = %14
  %17 = getelementptr inbounds nuw { ptr, ptr }, ptr %7, i32 0, i32 0
  %18 = extractvalue { ptr, ptr } %15, 0
  store ptr %18, ptr %17, align 8
  %19 = getelementptr inbounds nuw { ptr, ptr }, ptr %7, i32 0, i32 1
  %20 = extractvalue { ptr, ptr } %15, 1
  store ptr %20, ptr %19, align 8
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %10, ptr align 8 %7, i64 16, i1 false)
  %21 = getelementptr inbounds nuw { ptr, ptr }, ptr %10, i32 0, i32 0
  %22 = load ptr, ptr %21, align 8
  %23 = getelementptr inbounds nuw { ptr, ptr }, ptr %10, i32 0, i32 1
  %24 = load ptr, ptr %23, align 8
  %25 = invoke ptr @node_insert(ptr noundef nonnull align 8 dereferenceable(16) %6, ptr %22, ptr %24)
          to label %26 unwind label %30

26:                                               ; preds = %16
  %27 = getelementptr inbounds nuw %"struct.std::_Rb_tree_iterator", ptr %3, i32 0, i32 0
  store ptr %25, ptr %27, align 8
  call void @node_fini(ptr noundef nonnull align 8 dereferenceable(16) %6) #3
  %28 = getelementptr inbounds nuw %"struct.std::_Rb_tree_iterator", ptr %3, i32 0, i32 0
  %29 = load ptr, ptr %28, align 8
  ret ptr %29

30:                                               ; preds = %16, %14, %2
  %31 = landingpad { ptr, i32 }
          cleanup
  %32 = extractvalue { ptr, i32 } %31, 0
  store ptr %32, ptr %8, align 8
  %33 = extractvalue { ptr, i32 } %31, 1
  store i32 %33, ptr %9, align 4
  call void @node_fini(ptr noundef nonnull align 8 dereferenceable(16) %6) #3
  br label %34

34:                                               ; preds = %30
  %35 = load ptr, ptr %8, align 8
  %36 = load i32, ptr %9, align 4
  %37 = insertvalue { ptr, i32 } poison, ptr %35, 0
  %38 = insertvalue { ptr, i32 } %37, i32 %36, 1
  resume { ptr, i32 } %38
}

; Function Attrs: mustprogress noinline optnone sspstrong uwtable
declare void @node_init(ptr noundef nonnull align 8 dereferenceable(16), ptr noundef nonnull align 8 dereferenceable(48), ptr noundef nonnull align 8 dereferenceable(16)) unnamed_addr #0 align 2

; Function Attrs: mustprogress noinline optnone sspstrong uwtable
declare { ptr, ptr } @insert_pos(ptr noundef nonnull align 8 dereferenceable(48), ptr noundef nonnull align 8 dereferenceable(8)) #0 align 2

; Function Attrs: mustprogress noinline optnone sspstrong uwtable
declare noundef nonnull align 8 dereferenceable(8) ptr @node_key(ptr noundef nonnull align 8 dereferenceable(16)) #0 align 2

; Function Attrs: mustprogress noinline optnone sspstrong uwtable
declare ptr @node_insert(ptr noundef nonnull align 8 dereferenceable(16), ptr, ptr) #0 align 2

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i64, i1 immarg) #1

; Function Attrs: mustprogress noinline nounwind optnone sspstrong uwtable
declare void @node_fini(ptr noundef nonnull align 8 dereferenceable(16)) unnamed_addr #2 align 2

attributes #0 = { mustprogress noinline optnone sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress noinline nounwind optnone sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #3 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 4, !"probe-stack", !"inline-asm"}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 20.1.8"}
