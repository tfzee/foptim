; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; Returning {<2 x float>, <2 x float>}: the second half must come from the upper half of the
; constant and the first return value must not be replaced by the second one.
; IntrinSimplify forwards the loads of the local copy to the constant itself, so the
; second half is loaded from the constant at offset 8 (handwritten, the ExtractHigh
; of the constant is not reached from this input anymore).
; CHECK-LABEL: func _Z7testfun4Test
; CHECK-LABEL: func main
; CHECK: lea($rax: i64, [__const.main.d]: i64, )
; CHECK-NEXT: $rax: i64 += 8: i64
; CHECK: $mm1: f32x2 = [$rax]: f32x2
; CHECK-NEXT: call(_Z7testfun4Test
; ModuleID = '/home/tim/programming/foptim_test/test/multi_return_float.cpp'
source_filename = "/home/tim/programming/foptim_test/test/multi_return_float.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.Test = type { float, float, float, float }

$_ZSt3absf = comdat any

@__const.main.d = private unnamed_addr constant %struct.Test { float -1.000000e+00, float -2.000000e+00, float 3.000000e+00, float 4.000000e+00 }, align 4

; Function Attrs: mustprogress noinline nounwind optnone sspstrong uwtable
define { <2 x float>, <2 x float> } @_Z7testfun4Test(<2 x float> %0, <2 x float> %1) #0 {
  %3 = alloca %struct.Test, align 4
  %4 = alloca %struct.Test, align 4
  %5 = getelementptr inbounds nuw { <2 x float>, <2 x float> }, ptr %4, i32 0, i32 0
  store <2 x float> %0, ptr %5, align 4
  %6 = getelementptr inbounds nuw { <2 x float>, <2 x float> }, ptr %4, i32 0, i32 1
  store <2 x float> %1, ptr %6, align 4
  %7 = getelementptr inbounds nuw %struct.Test, ptr %3, i32 0, i32 0
  %8 = getelementptr inbounds nuw %struct.Test, ptr %4, i32 0, i32 0
  %9 = load float, ptr %8, align 4
  %10 = call noundef float @_ZSt3absf(float noundef %9)
  store float %10, ptr %7, align 4
  %11 = getelementptr inbounds nuw %struct.Test, ptr %3, i32 0, i32 1
  %12 = getelementptr inbounds nuw %struct.Test, ptr %4, i32 0, i32 1
  %13 = load float, ptr %12, align 4
  %14 = call noundef float @_ZSt3absf(float noundef %13)
  store float %14, ptr %11, align 4
  %15 = getelementptr inbounds nuw %struct.Test, ptr %3, i32 0, i32 2
  %16 = getelementptr inbounds nuw %struct.Test, ptr %4, i32 0, i32 2
  %17 = load float, ptr %16, align 4
  %18 = call noundef float @_ZSt3absf(float noundef %17)
  store float %18, ptr %15, align 4
  %19 = getelementptr inbounds nuw %struct.Test, ptr %3, i32 0, i32 3
  %20 = getelementptr inbounds nuw %struct.Test, ptr %4, i32 0, i32 3
  %21 = load float, ptr %20, align 4
  %22 = call noundef float @_ZSt3absf(float noundef %21)
  store float %22, ptr %19, align 4
  %23 = load { <2 x float>, <2 x float> }, ptr %3, align 4
  ret { <2 x float>, <2 x float> } %23
}

; Function Attrs: mustprogress noinline nounwind optnone sspstrong uwtable
define linkonce_odr noundef float @_ZSt3absf(float noundef %0) #1 comdat {
  %2 = alloca float, align 4
  store float %0, ptr %2, align 4
  %3 = load float, ptr %2, align 4
  %4 = call float @llvm.fabs.f32(float %3)
  ret float %4
}

; Function Attrs: mustprogress noinline norecurse nounwind optnone sspstrong uwtable
define noundef i32 @main() #2 {
  %1 = alloca i32, align 4
  %2 = alloca %struct.Test, align 4
  %3 = alloca %struct.Test, align 4
  %4 = alloca %struct.Test, align 4
  store i32 0, ptr %1, align 4
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %2, ptr align 4 @__const.main.d, i64 16, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %4, ptr align 4 %2, i64 16, i1 false)
  %5 = getelementptr inbounds nuw { <2 x float>, <2 x float> }, ptr %4, i32 0, i32 0
  %6 = load <2 x float>, ptr %5, align 4
  %7 = getelementptr inbounds nuw { <2 x float>, <2 x float> }, ptr %4, i32 0, i32 1
  %8 = load <2 x float>, ptr %7, align 4
  %9 = call { <2 x float>, <2 x float> } @_Z7testfun4Test(<2 x float> %6, <2 x float> %8)
  %10 = getelementptr inbounds nuw { <2 x float>, <2 x float> }, ptr %3, i32 0, i32 0
  %11 = extractvalue { <2 x float>, <2 x float> } %9, 0
  store <2 x float> %11, ptr %10, align 4
  %12 = getelementptr inbounds nuw { <2 x float>, <2 x float> }, ptr %3, i32 0, i32 1
  %13 = extractvalue { <2 x float>, <2 x float> } %9, 1
  store <2 x float> %13, ptr %12, align 4
  %14 = getelementptr inbounds nuw %struct.Test, ptr %3, i32 0, i32 0
  %15 = load float, ptr %14, align 4
  %16 = getelementptr inbounds nuw %struct.Test, ptr %3, i32 0, i32 1
  %17 = load float, ptr %16, align 4
  %18 = fadd float %15, %17
  %19 = getelementptr inbounds nuw %struct.Test, ptr %3, i32 0, i32 2
  %20 = load float, ptr %19, align 4
  %21 = fadd float %18, %20
  %22 = getelementptr inbounds nuw %struct.Test, ptr %3, i32 0, i32 3
  %23 = load float, ptr %22, align 4
  %24 = fadd float %21, %23
  %25 = fptosi float %24 to i32
  ret i32 %25
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i64, i1 immarg) #3

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fabs.f32(float) #4

attributes #0 = { mustprogress noinline nounwind optnone sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="64" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #1 = { mustprogress noinline nounwind optnone sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #2 = { mustprogress noinline norecurse nounwind optnone sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="64" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #4 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 4, !"probe-stack", !"inline-asm"}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 20.1.8"}
