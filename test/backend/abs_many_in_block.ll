; RUN: %foffcc %s %t.o
; The destination of abs must differ from its source, this used to make the destination live up to the start of the
; block. With many abs results in one block behind a call the interference graph then had no solution and register
; allocation did not converge. The test only checks that the function compiles (a hand written RUN line on purpose,
; the MIR of the function is a few hundred lines of spills).
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nounwind sspstrong uwtable
define i32 @sad3(ptr noundef %0, ptr noundef %1, ptr noundef %2, ptr noundef %3) #0 {
  %5 = alloca ptr, align 8
  %6 = alloca ptr, align 8
  %7 = alloca ptr, align 8
  %8 = alloca ptr, align 8
  %9 = alloca [3 x i32], align 4
  %10 = alloca i32, align 4
  %11 = alloca i32, align 4
  store ptr %0, ptr %5, align 8, !tbaa !10
  store ptr %1, ptr %6, align 8, !tbaa !10
  store ptr %2, ptr %7, align 8, !tbaa !10
  store ptr %3, ptr %8, align 8, !tbaa !13
  call void @llvm.lifetime.start.p0(ptr %9) #4
  call void @llvm.lifetime.start.p0(ptr %10) #4
  store i32 0, ptr %10, align 4, !tbaa !6
  br label %12

12:                                               ; preds = %214, %4
  %13 = load i32, ptr %10, align 4, !tbaa !6
  %14 = icmp slt i32 %13, 3
  br i1 %14, label %16, label %15

15:                                               ; preds = %12
  call void @llvm.lifetime.end.p0(ptr %10) #4
  br label %217

16:                                               ; preds = %12
  %17 = load ptr, ptr %6, align 8, !tbaa !10
  call void @pred(ptr noundef %17)
  call void @llvm.lifetime.start.p0(ptr %11) #4
  store i32 0, ptr %11, align 4, !tbaa !6
  %18 = load ptr, ptr %5, align 8, !tbaa !10
  %19 = getelementptr inbounds i8, ptr %18, i64 0
  %20 = load i8, ptr %19, align 1, !tbaa !15
  %21 = zext i8 %20 to i32
  %22 = load ptr, ptr %6, align 8, !tbaa !10
  %23 = getelementptr inbounds i8, ptr %22, i64 0
  %24 = load i8, ptr %23, align 1, !tbaa !15
  %25 = zext i8 %24 to i32
  %26 = sub i32 %21, %25
  %27 = call i32 @llvm.abs.i32(i32 %26, i1 false)
  %28 = load i32, ptr %11, align 4, !tbaa !6
  %29 = add i32 %28, %27
  store i32 %29, ptr %11, align 4, !tbaa !6
  %30 = load ptr, ptr %5, align 8, !tbaa !10
  %31 = getelementptr inbounds i8, ptr %30, i64 1
  %32 = load i8, ptr %31, align 1, !tbaa !15
  %33 = zext i8 %32 to i32
  %34 = load ptr, ptr %6, align 8, !tbaa !10
  %35 = getelementptr inbounds i8, ptr %34, i64 1
  %36 = load i8, ptr %35, align 1, !tbaa !15
  %37 = zext i8 %36 to i32
  %38 = sub i32 %33, %37
  %39 = call i32 @llvm.abs.i32(i32 %38, i1 false)
  %40 = load i32, ptr %11, align 4, !tbaa !6
  %41 = add i32 %40, %39
  store i32 %41, ptr %11, align 4, !tbaa !6
  %42 = load ptr, ptr %5, align 8, !tbaa !10
  %43 = getelementptr inbounds i8, ptr %42, i64 2
  %44 = load i8, ptr %43, align 1, !tbaa !15
  %45 = zext i8 %44 to i32
  %46 = load ptr, ptr %6, align 8, !tbaa !10
  %47 = getelementptr inbounds i8, ptr %46, i64 2
  %48 = load i8, ptr %47, align 1, !tbaa !15
  %49 = zext i8 %48 to i32
  %50 = sub i32 %45, %49
  %51 = call i32 @llvm.abs.i32(i32 %50, i1 false)
  %52 = load i32, ptr %11, align 4, !tbaa !6
  %53 = add i32 %52, %51
  store i32 %53, ptr %11, align 4, !tbaa !6
  %54 = load ptr, ptr %5, align 8, !tbaa !10
  %55 = getelementptr inbounds i8, ptr %54, i64 3
  %56 = load i8, ptr %55, align 1, !tbaa !15
  %57 = zext i8 %56 to i32
  %58 = load ptr, ptr %6, align 8, !tbaa !10
  %59 = getelementptr inbounds i8, ptr %58, i64 3
  %60 = load i8, ptr %59, align 1, !tbaa !15
  %61 = zext i8 %60 to i32
  %62 = sub i32 %57, %61
  %63 = call i32 @llvm.abs.i32(i32 %62, i1 false)
  %64 = load i32, ptr %11, align 4, !tbaa !6
  %65 = add i32 %64, %63
  store i32 %65, ptr %11, align 4, !tbaa !6
  %66 = load ptr, ptr %5, align 8, !tbaa !10
  %67 = getelementptr inbounds i8, ptr %66, i64 4
  %68 = load i8, ptr %67, align 1, !tbaa !15
  %69 = zext i8 %68 to i32
  %70 = load ptr, ptr %6, align 8, !tbaa !10
  %71 = getelementptr inbounds i8, ptr %70, i64 4
  %72 = load i8, ptr %71, align 1, !tbaa !15
  %73 = zext i8 %72 to i32
  %74 = sub i32 %69, %73
  %75 = call i32 @llvm.abs.i32(i32 %74, i1 false)
  %76 = load i32, ptr %11, align 4, !tbaa !6
  %77 = add i32 %76, %75
  store i32 %77, ptr %11, align 4, !tbaa !6
  %78 = load ptr, ptr %5, align 8, !tbaa !10
  %79 = getelementptr inbounds i8, ptr %78, i64 5
  %80 = load i8, ptr %79, align 1, !tbaa !15
  %81 = zext i8 %80 to i32
  %82 = load ptr, ptr %6, align 8, !tbaa !10
  %83 = getelementptr inbounds i8, ptr %82, i64 5
  %84 = load i8, ptr %83, align 1, !tbaa !15
  %85 = zext i8 %84 to i32
  %86 = sub i32 %81, %85
  %87 = call i32 @llvm.abs.i32(i32 %86, i1 false)
  %88 = load i32, ptr %11, align 4, !tbaa !6
  %89 = add i32 %88, %87
  store i32 %89, ptr %11, align 4, !tbaa !6
  %90 = load ptr, ptr %5, align 8, !tbaa !10
  %91 = getelementptr inbounds i8, ptr %90, i64 6
  %92 = load i8, ptr %91, align 1, !tbaa !15
  %93 = zext i8 %92 to i32
  %94 = load ptr, ptr %6, align 8, !tbaa !10
  %95 = getelementptr inbounds i8, ptr %94, i64 6
  %96 = load i8, ptr %95, align 1, !tbaa !15
  %97 = zext i8 %96 to i32
  %98 = sub i32 %93, %97
  %99 = call i32 @llvm.abs.i32(i32 %98, i1 false)
  %100 = load i32, ptr %11, align 4, !tbaa !6
  %101 = add i32 %100, %99
  store i32 %101, ptr %11, align 4, !tbaa !6
  %102 = load ptr, ptr %5, align 8, !tbaa !10
  %103 = getelementptr inbounds i8, ptr %102, i64 7
  %104 = load i8, ptr %103, align 1, !tbaa !15
  %105 = zext i8 %104 to i32
  %106 = load ptr, ptr %6, align 8, !tbaa !10
  %107 = getelementptr inbounds i8, ptr %106, i64 7
  %108 = load i8, ptr %107, align 1, !tbaa !15
  %109 = zext i8 %108 to i32
  %110 = sub i32 %105, %109
  %111 = call i32 @llvm.abs.i32(i32 %110, i1 false)
  %112 = load i32, ptr %11, align 4, !tbaa !6
  %113 = add i32 %112, %111
  store i32 %113, ptr %11, align 4, !tbaa !6
  %114 = load ptr, ptr %5, align 8, !tbaa !10
  %115 = getelementptr inbounds i8, ptr %114, i64 8
  %116 = load i8, ptr %115, align 1, !tbaa !15
  %117 = zext i8 %116 to i32
  %118 = load ptr, ptr %6, align 8, !tbaa !10
  %119 = getelementptr inbounds i8, ptr %118, i64 8
  %120 = load i8, ptr %119, align 1, !tbaa !15
  %121 = zext i8 %120 to i32
  %122 = sub i32 %117, %121
  %123 = call i32 @llvm.abs.i32(i32 %122, i1 false)
  %124 = load i32, ptr %11, align 4, !tbaa !6
  %125 = add i32 %124, %123
  store i32 %125, ptr %11, align 4, !tbaa !6
  %126 = load ptr, ptr %5, align 8, !tbaa !10
  %127 = getelementptr inbounds i8, ptr %126, i64 9
  %128 = load i8, ptr %127, align 1, !tbaa !15
  %129 = zext i8 %128 to i32
  %130 = load ptr, ptr %6, align 8, !tbaa !10
  %131 = getelementptr inbounds i8, ptr %130, i64 9
  %132 = load i8, ptr %131, align 1, !tbaa !15
  %133 = zext i8 %132 to i32
  %134 = sub i32 %129, %133
  %135 = call i32 @llvm.abs.i32(i32 %134, i1 false)
  %136 = load i32, ptr %11, align 4, !tbaa !6
  %137 = add i32 %136, %135
  store i32 %137, ptr %11, align 4, !tbaa !6
  %138 = load ptr, ptr %5, align 8, !tbaa !10
  %139 = getelementptr inbounds i8, ptr %138, i64 10
  %140 = load i8, ptr %139, align 1, !tbaa !15
  %141 = zext i8 %140 to i32
  %142 = load ptr, ptr %6, align 8, !tbaa !10
  %143 = getelementptr inbounds i8, ptr %142, i64 10
  %144 = load i8, ptr %143, align 1, !tbaa !15
  %145 = zext i8 %144 to i32
  %146 = sub i32 %141, %145
  %147 = call i32 @llvm.abs.i32(i32 %146, i1 false)
  %148 = load i32, ptr %11, align 4, !tbaa !6
  %149 = add i32 %148, %147
  store i32 %149, ptr %11, align 4, !tbaa !6
  %150 = load ptr, ptr %5, align 8, !tbaa !10
  %151 = getelementptr inbounds i8, ptr %150, i64 11
  %152 = load i8, ptr %151, align 1, !tbaa !15
  %153 = zext i8 %152 to i32
  %154 = load ptr, ptr %6, align 8, !tbaa !10
  %155 = getelementptr inbounds i8, ptr %154, i64 11
  %156 = load i8, ptr %155, align 1, !tbaa !15
  %157 = zext i8 %156 to i32
  %158 = sub i32 %153, %157
  %159 = call i32 @llvm.abs.i32(i32 %158, i1 false)
  %160 = load i32, ptr %11, align 4, !tbaa !6
  %161 = add i32 %160, %159
  store i32 %161, ptr %11, align 4, !tbaa !6
  %162 = load ptr, ptr %5, align 8, !tbaa !10
  %163 = getelementptr inbounds i8, ptr %162, i64 12
  %164 = load i8, ptr %163, align 1, !tbaa !15
  %165 = zext i8 %164 to i32
  %166 = load ptr, ptr %6, align 8, !tbaa !10
  %167 = getelementptr inbounds i8, ptr %166, i64 12
  %168 = load i8, ptr %167, align 1, !tbaa !15
  %169 = zext i8 %168 to i32
  %170 = sub i32 %165, %169
  %171 = call i32 @llvm.abs.i32(i32 %170, i1 false)
  %172 = load i32, ptr %11, align 4, !tbaa !6
  %173 = add i32 %172, %171
  store i32 %173, ptr %11, align 4, !tbaa !6
  %174 = load ptr, ptr %5, align 8, !tbaa !10
  %175 = getelementptr inbounds i8, ptr %174, i64 13
  %176 = load i8, ptr %175, align 1, !tbaa !15
  %177 = zext i8 %176 to i32
  %178 = load ptr, ptr %6, align 8, !tbaa !10
  %179 = getelementptr inbounds i8, ptr %178, i64 13
  %180 = load i8, ptr %179, align 1, !tbaa !15
  %181 = zext i8 %180 to i32
  %182 = sub i32 %177, %181
  %183 = call i32 @llvm.abs.i32(i32 %182, i1 false)
  %184 = load i32, ptr %11, align 4, !tbaa !6
  %185 = add i32 %184, %183
  store i32 %185, ptr %11, align 4, !tbaa !6
  %186 = load ptr, ptr %5, align 8, !tbaa !10
  %187 = getelementptr inbounds i8, ptr %186, i64 14
  %188 = load i8, ptr %187, align 1, !tbaa !15
  %189 = zext i8 %188 to i32
  %190 = load ptr, ptr %6, align 8, !tbaa !10
  %191 = getelementptr inbounds i8, ptr %190, i64 14
  %192 = load i8, ptr %191, align 1, !tbaa !15
  %193 = zext i8 %192 to i32
  %194 = sub i32 %189, %193
  %195 = call i32 @llvm.abs.i32(i32 %194, i1 false)
  %196 = load i32, ptr %11, align 4, !tbaa !6
  %197 = add i32 %196, %195
  store i32 %197, ptr %11, align 4, !tbaa !6
  %198 = load ptr, ptr %5, align 8, !tbaa !10
  %199 = getelementptr inbounds i8, ptr %198, i64 15
  %200 = load i8, ptr %199, align 1, !tbaa !15
  %201 = zext i8 %200 to i32
  %202 = load ptr, ptr %6, align 8, !tbaa !10
  %203 = getelementptr inbounds i8, ptr %202, i64 15
  %204 = load i8, ptr %203, align 1, !tbaa !15
  %205 = zext i8 %204 to i32
  %206 = sub i32 %201, %205
  %207 = call i32 @llvm.abs.i32(i32 %206, i1 false)
  %208 = load i32, ptr %11, align 4, !tbaa !6
  %209 = add i32 %208, %207
  store i32 %209, ptr %11, align 4, !tbaa !6
  %210 = load i32, ptr %11, align 4, !tbaa !6
  %211 = load i32, ptr %10, align 4, !tbaa !6
  %212 = sext i32 %211 to i64
  %213 = getelementptr inbounds [3 x i32], ptr %9, i64 0, i64 %212
  store i32 %210, ptr %213, align 4, !tbaa !6
  call void @llvm.lifetime.end.p0(ptr %11) #4
  br label %214

214:                                              ; preds = %16
  %215 = load i32, ptr %10, align 4, !tbaa !6
  %216 = add i32 %215, 1
  store i32 %216, ptr %10, align 4, !tbaa !6
  br label %12, !llvm.loop !16

217:                                              ; preds = %15
  %218 = getelementptr inbounds [3 x i32], ptr %9, i64 0, i64 0
  %219 = load i32, ptr %218, align 4, !tbaa !6
  %220 = load ptr, ptr %8, align 8, !tbaa !13
  %221 = getelementptr inbounds i32, ptr %220, i64 0
  store i32 %219, ptr %221, align 4, !tbaa !6
  %222 = getelementptr inbounds [3 x i32], ptr %9, i64 0, i64 1
  %223 = load i32, ptr %222, align 4, !tbaa !6
  %224 = load ptr, ptr %8, align 8, !tbaa !13
  %225 = getelementptr inbounds i32, ptr %224, i64 1
  store i32 %223, ptr %225, align 4, !tbaa !6
  %226 = getelementptr inbounds [3 x i32], ptr %9, i64 0, i64 2
  %227 = load i32, ptr %226, align 4, !tbaa !6
  %228 = load ptr, ptr %8, align 8, !tbaa !13
  %229 = getelementptr inbounds i32, ptr %228, i64 2
  store i32 %227, ptr %229, align 4, !tbaa !6
  call void @llvm.lifetime.end.p0(ptr %9) #4
  ret i32 0
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

declare void @pred(ptr noundef) #2

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.abs.i32(i32, i1 immarg) #3

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

attributes #0 = { nounwind sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #3 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #4 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}
!llvm.errno.tbaa = !{!6}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 4, !"probe-stack", !"inline-asm"}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 22.1.8"}
!6 = !{!7, !7, i64 0}
!7 = !{!"int", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
!10 = !{!11, !11, i64 0}
!11 = !{!"p1 omnipotent char", !12, i64 0}
!12 = !{!"any pointer", !8, i64 0}
!13 = !{!14, !14, i64 0}
!14 = !{!"p1 int", !12, i64 0}
!15 = !{!8, !8, i64 0}
!16 = distinct !{!16, !17, !18}
!17 = !{!"llvm.loop.mustprogress"}
!18 = !{!"llvm.loop.unroll.disable"}
