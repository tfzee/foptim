; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; Handwritten CHECKs: the test pins the frame layout of a variadic function
; with spill slots, not the whole body. va_start hands out rbp - 176 as the
; register save area, so the sub rsp of the spill slots has to come after the
; one of the save area and the slots have to live below it (the register
; stores and the first spill must not overlap).
; CHECK-LABEL: func f
; CHECK: $rsp: i64 -= 176
; CHECK-NEXT: $rsp: i64 -= {{[0-9]+}}
; CHECK: [$rbp + -176]: i64 = $rdi
; CHECK: [$rbp + -{{(1[89][0-9]|[2-9][0-9][0-9])}}]: i64 = $rax
; ModuleID = 'va4.c'
source_filename = "va4.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.__va_list_tag = type { i32, i32, ptr, ptr }

; Function Attrs: noinline nounwind sspstrong uwtable
define i64 @f(i32 noundef %0, ...) #0 {
  %2 = alloca i32, align 4
  %3 = alloca [1 x %struct.__va_list_tag], align 16
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  %6 = alloca i64, align 8
  %7 = alloca i64, align 8
  %8 = alloca i64, align 8
  %9 = alloca i64, align 8
  %10 = alloca i64, align 8
  %11 = alloca i64, align 8
  %12 = alloca i64, align 8
  %13 = alloca i64, align 8
  %14 = alloca i64, align 8
  %15 = alloca i64, align 8
  %16 = alloca i64, align 8
  %17 = alloca i64, align 8
  %18 = alloca i64, align 8
  %19 = alloca i64, align 8
  %20 = alloca i64, align 8
  %21 = alloca i64, align 8
  %22 = alloca i64, align 8
  %23 = alloca i64, align 8
  %24 = alloca i64, align 8
  %25 = alloca i64, align 8
  %26 = alloca i64, align 8
  %27 = alloca i64, align 8
  %28 = alloca i64, align 8
  %29 = alloca i64, align 8
  %30 = alloca i64, align 8
  %31 = alloca i64, align 8
  %32 = alloca i64, align 8
  %33 = alloca i64, align 8
  %34 = alloca i64, align 8
  %35 = alloca i64, align 8
  %36 = alloca i64, align 8
  %37 = alloca i64, align 8
  %38 = alloca i64, align 8
  %39 = alloca i64, align 8
  %40 = alloca i64, align 8
  %41 = alloca i64, align 8
  %42 = alloca i64, align 8
  %43 = alloca i64, align 8
  %44 = alloca i64, align 8
  %45 = alloca i32, align 4
  store i32 %0, ptr %2, align 4, !tbaa !6
  call void @llvm.lifetime.start.p0(ptr %3) #4
  %46 = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %3, i64 0, i64 0
  call void @llvm.va_start.p0(ptr %46)
  call void @llvm.lifetime.start.p0(ptr %4) #4
  %47 = call i64 @ext(i64 noundef 0)
  store i64 %47, ptr %4, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %5) #4
  %48 = call i64 @ext(i64 noundef 1)
  store i64 %48, ptr %5, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %6) #4
  %49 = call i64 @ext(i64 noundef 2)
  store i64 %49, ptr %6, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %7) #4
  %50 = call i64 @ext(i64 noundef 3)
  store i64 %50, ptr %7, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %8) #4
  %51 = call i64 @ext(i64 noundef 4)
  store i64 %51, ptr %8, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %9) #4
  %52 = call i64 @ext(i64 noundef 5)
  store i64 %52, ptr %9, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %10) #4
  %53 = call i64 @ext(i64 noundef 6)
  store i64 %53, ptr %10, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %11) #4
  %54 = call i64 @ext(i64 noundef 7)
  store i64 %54, ptr %11, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %12) #4
  %55 = call i64 @ext(i64 noundef 8)
  store i64 %55, ptr %12, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %13) #4
  %56 = call i64 @ext(i64 noundef 9)
  store i64 %56, ptr %13, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %14) #4
  %57 = call i64 @ext(i64 noundef 10)
  store i64 %57, ptr %14, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %15) #4
  %58 = call i64 @ext(i64 noundef 11)
  store i64 %58, ptr %15, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %16) #4
  %59 = call i64 @ext(i64 noundef 12)
  store i64 %59, ptr %16, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %17) #4
  %60 = call i64 @ext(i64 noundef 13)
  store i64 %60, ptr %17, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %18) #4
  %61 = call i64 @ext(i64 noundef 14)
  store i64 %61, ptr %18, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %19) #4
  %62 = call i64 @ext(i64 noundef 15)
  store i64 %62, ptr %19, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %20) #4
  %63 = call i64 @ext(i64 noundef 16)
  store i64 %63, ptr %20, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %21) #4
  %64 = call i64 @ext(i64 noundef 17)
  store i64 %64, ptr %21, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %22) #4
  %65 = call i64 @ext(i64 noundef 18)
  store i64 %65, ptr %22, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %23) #4
  %66 = call i64 @ext(i64 noundef 19)
  store i64 %66, ptr %23, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %24) #4
  %67 = call i64 @ext(i64 noundef 20)
  store i64 %67, ptr %24, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %25) #4
  %68 = call i64 @ext(i64 noundef 21)
  store i64 %68, ptr %25, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %26) #4
  %69 = call i64 @ext(i64 noundef 22)
  store i64 %69, ptr %26, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %27) #4
  %70 = call i64 @ext(i64 noundef 23)
  store i64 %70, ptr %27, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %28) #4
  %71 = call i64 @ext(i64 noundef 24)
  store i64 %71, ptr %28, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %29) #4
  %72 = call i64 @ext(i64 noundef 25)
  store i64 %72, ptr %29, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %30) #4
  %73 = call i64 @ext(i64 noundef 26)
  store i64 %73, ptr %30, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %31) #4
  %74 = call i64 @ext(i64 noundef 27)
  store i64 %74, ptr %31, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %32) #4
  %75 = call i64 @ext(i64 noundef 28)
  store i64 %75, ptr %32, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %33) #4
  %76 = call i64 @ext(i64 noundef 29)
  store i64 %76, ptr %33, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %34) #4
  %77 = call i64 @ext(i64 noundef 30)
  store i64 %77, ptr %34, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %35) #4
  %78 = call i64 @ext(i64 noundef 31)
  store i64 %78, ptr %35, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %36) #4
  %79 = call i64 @ext(i64 noundef 32)
  store i64 %79, ptr %36, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %37) #4
  %80 = call i64 @ext(i64 noundef 33)
  store i64 %80, ptr %37, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %38) #4
  %81 = call i64 @ext(i64 noundef 34)
  store i64 %81, ptr %38, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %39) #4
  %82 = call i64 @ext(i64 noundef 35)
  store i64 %82, ptr %39, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %40) #4
  %83 = call i64 @ext(i64 noundef 36)
  store i64 %83, ptr %40, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %41) #4
  %84 = call i64 @ext(i64 noundef 37)
  store i64 %84, ptr %41, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %42) #4
  %85 = call i64 @ext(i64 noundef 38)
  store i64 %85, ptr %42, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %43) #4
  %86 = call i64 @ext(i64 noundef 39)
  store i64 %86, ptr %43, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %44) #4
  store i64 0, ptr %44, align 8, !tbaa !10
  call void @llvm.lifetime.start.p0(ptr %45) #4
  store i32 0, ptr %45, align 4, !tbaa !6
  br label %87

87:                                               ; preds = %111, %1
  %88 = load i32, ptr %45, align 4, !tbaa !6
  %89 = load i32, ptr %2, align 4, !tbaa !6
  %90 = icmp slt i32 %88, %89
  br i1 %90, label %92, label %91

91:                                               ; preds = %87
  call void @llvm.lifetime.end.p0(ptr %45) #4
  br label %114

92:                                               ; preds = %87
  %93 = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %3, i64 0, i64 0
  %94 = getelementptr inbounds nuw %struct.__va_list_tag, ptr %93, i32 0, i32 0
  %95 = load i32, ptr %94, align 16
  %96 = icmp ule i32 %95, 40
  br i1 %96, label %97, label %102

97:                                               ; preds = %92
  %98 = getelementptr inbounds nuw %struct.__va_list_tag, ptr %93, i32 0, i32 3
  %99 = load ptr, ptr %98, align 16
  %100 = getelementptr i8, ptr %99, i32 %95
  %101 = add i32 %95, 8
  store i32 %101, ptr %94, align 16
  br label %106

102:                                              ; preds = %92
  %103 = getelementptr inbounds nuw %struct.__va_list_tag, ptr %93, i32 0, i32 2
  %104 = load ptr, ptr %103, align 8
  %105 = getelementptr i8, ptr %104, i32 8
  store ptr %105, ptr %103, align 8
  br label %106

106:                                              ; preds = %102, %97
  %107 = phi ptr [ %100, %97 ], [ %104, %102 ]
  %108 = load i64, ptr %107, align 8, !tbaa !10
  %109 = load i64, ptr %44, align 8, !tbaa !10
  %110 = add i64 %109, %108
  store i64 %110, ptr %44, align 8, !tbaa !10
  br label %111

111:                                              ; preds = %106
  %112 = load i32, ptr %45, align 4, !tbaa !6
  %113 = add i32 %112, 1
  store i32 %113, ptr %45, align 4, !tbaa !6
  br label %87, !llvm.loop !12

114:                                              ; preds = %91
  %115 = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %3, i64 0, i64 0
  call void @llvm.va_end.p0(ptr %115)
  %116 = load i64, ptr %44, align 8, !tbaa !10
  %117 = load i64, ptr %4, align 8, !tbaa !10
  %118 = load i64, ptr %11, align 8, !tbaa !10
  %119 = mul i64 %117, %118
  %120 = add i64 %116, %119
  %121 = load i64, ptr %5, align 8, !tbaa !10
  %122 = load i64, ptr %12, align 8, !tbaa !10
  %123 = mul i64 %121, %122
  %124 = add i64 %120, %123
  %125 = load i64, ptr %6, align 8, !tbaa !10
  %126 = load i64, ptr %13, align 8, !tbaa !10
  %127 = mul i64 %125, %126
  %128 = add i64 %124, %127
  %129 = load i64, ptr %7, align 8, !tbaa !10
  %130 = load i64, ptr %14, align 8, !tbaa !10
  %131 = mul i64 %129, %130
  %132 = add i64 %128, %131
  %133 = load i64, ptr %8, align 8, !tbaa !10
  %134 = load i64, ptr %15, align 8, !tbaa !10
  %135 = mul i64 %133, %134
  %136 = add i64 %132, %135
  %137 = load i64, ptr %9, align 8, !tbaa !10
  %138 = load i64, ptr %16, align 8, !tbaa !10
  %139 = mul i64 %137, %138
  %140 = add i64 %136, %139
  %141 = load i64, ptr %10, align 8, !tbaa !10
  %142 = load i64, ptr %17, align 8, !tbaa !10
  %143 = mul i64 %141, %142
  %144 = add i64 %140, %143
  %145 = load i64, ptr %11, align 8, !tbaa !10
  %146 = load i64, ptr %18, align 8, !tbaa !10
  %147 = mul i64 %145, %146
  %148 = add i64 %144, %147
  %149 = load i64, ptr %12, align 8, !tbaa !10
  %150 = load i64, ptr %19, align 8, !tbaa !10
  %151 = mul i64 %149, %150
  %152 = add i64 %148, %151
  %153 = load i64, ptr %13, align 8, !tbaa !10
  %154 = load i64, ptr %20, align 8, !tbaa !10
  %155 = mul i64 %153, %154
  %156 = add i64 %152, %155
  %157 = load i64, ptr %14, align 8, !tbaa !10
  %158 = load i64, ptr %21, align 8, !tbaa !10
  %159 = mul i64 %157, %158
  %160 = add i64 %156, %159
  %161 = load i64, ptr %15, align 8, !tbaa !10
  %162 = load i64, ptr %22, align 8, !tbaa !10
  %163 = mul i64 %161, %162
  %164 = add i64 %160, %163
  %165 = load i64, ptr %16, align 8, !tbaa !10
  %166 = load i64, ptr %23, align 8, !tbaa !10
  %167 = mul i64 %165, %166
  %168 = add i64 %164, %167
  %169 = load i64, ptr %17, align 8, !tbaa !10
  %170 = load i64, ptr %24, align 8, !tbaa !10
  %171 = mul i64 %169, %170
  %172 = add i64 %168, %171
  %173 = load i64, ptr %18, align 8, !tbaa !10
  %174 = load i64, ptr %25, align 8, !tbaa !10
  %175 = mul i64 %173, %174
  %176 = add i64 %172, %175
  %177 = load i64, ptr %19, align 8, !tbaa !10
  %178 = load i64, ptr %26, align 8, !tbaa !10
  %179 = mul i64 %177, %178
  %180 = add i64 %176, %179
  %181 = load i64, ptr %20, align 8, !tbaa !10
  %182 = load i64, ptr %27, align 8, !tbaa !10
  %183 = mul i64 %181, %182
  %184 = add i64 %180, %183
  %185 = load i64, ptr %21, align 8, !tbaa !10
  %186 = load i64, ptr %28, align 8, !tbaa !10
  %187 = mul i64 %185, %186
  %188 = add i64 %184, %187
  %189 = load i64, ptr %22, align 8, !tbaa !10
  %190 = load i64, ptr %29, align 8, !tbaa !10
  %191 = mul i64 %189, %190
  %192 = add i64 %188, %191
  %193 = load i64, ptr %23, align 8, !tbaa !10
  %194 = load i64, ptr %30, align 8, !tbaa !10
  %195 = mul i64 %193, %194
  %196 = add i64 %192, %195
  %197 = load i64, ptr %24, align 8, !tbaa !10
  %198 = load i64, ptr %31, align 8, !tbaa !10
  %199 = mul i64 %197, %198
  %200 = add i64 %196, %199
  %201 = load i64, ptr %25, align 8, !tbaa !10
  %202 = load i64, ptr %32, align 8, !tbaa !10
  %203 = mul i64 %201, %202
  %204 = add i64 %200, %203
  %205 = load i64, ptr %26, align 8, !tbaa !10
  %206 = load i64, ptr %33, align 8, !tbaa !10
  %207 = mul i64 %205, %206
  %208 = add i64 %204, %207
  %209 = load i64, ptr %27, align 8, !tbaa !10
  %210 = load i64, ptr %34, align 8, !tbaa !10
  %211 = mul i64 %209, %210
  %212 = add i64 %208, %211
  %213 = load i64, ptr %28, align 8, !tbaa !10
  %214 = load i64, ptr %35, align 8, !tbaa !10
  %215 = mul i64 %213, %214
  %216 = add i64 %212, %215
  %217 = load i64, ptr %29, align 8, !tbaa !10
  %218 = load i64, ptr %36, align 8, !tbaa !10
  %219 = mul i64 %217, %218
  %220 = add i64 %216, %219
  %221 = load i64, ptr %30, align 8, !tbaa !10
  %222 = load i64, ptr %37, align 8, !tbaa !10
  %223 = mul i64 %221, %222
  %224 = add i64 %220, %223
  %225 = load i64, ptr %31, align 8, !tbaa !10
  %226 = load i64, ptr %38, align 8, !tbaa !10
  %227 = mul i64 %225, %226
  %228 = add i64 %224, %227
  %229 = load i64, ptr %32, align 8, !tbaa !10
  %230 = load i64, ptr %39, align 8, !tbaa !10
  %231 = mul i64 %229, %230
  %232 = add i64 %228, %231
  %233 = load i64, ptr %33, align 8, !tbaa !10
  %234 = load i64, ptr %40, align 8, !tbaa !10
  %235 = mul i64 %233, %234
  %236 = add i64 %232, %235
  %237 = load i64, ptr %34, align 8, !tbaa !10
  %238 = load i64, ptr %41, align 8, !tbaa !10
  %239 = mul i64 %237, %238
  %240 = add i64 %236, %239
  %241 = load i64, ptr %35, align 8, !tbaa !10
  %242 = load i64, ptr %42, align 8, !tbaa !10
  %243 = mul i64 %241, %242
  %244 = add i64 %240, %243
  %245 = load i64, ptr %36, align 8, !tbaa !10
  %246 = load i64, ptr %43, align 8, !tbaa !10
  %247 = mul i64 %245, %246
  %248 = add i64 %244, %247
  %249 = load i64, ptr %37, align 8, !tbaa !10
  %250 = load i64, ptr %4, align 8, !tbaa !10
  %251 = mul i64 %249, %250
  %252 = add i64 %248, %251
  %253 = load i64, ptr %38, align 8, !tbaa !10
  %254 = load i64, ptr %5, align 8, !tbaa !10
  %255 = mul i64 %253, %254
  %256 = add i64 %252, %255
  %257 = load i64, ptr %39, align 8, !tbaa !10
  %258 = load i64, ptr %6, align 8, !tbaa !10
  %259 = mul i64 %257, %258
  %260 = add i64 %256, %259
  %261 = load i64, ptr %40, align 8, !tbaa !10
  %262 = load i64, ptr %7, align 8, !tbaa !10
  %263 = mul i64 %261, %262
  %264 = add i64 %260, %263
  %265 = load i64, ptr %41, align 8, !tbaa !10
  %266 = load i64, ptr %8, align 8, !tbaa !10
  %267 = mul i64 %265, %266
  %268 = add i64 %264, %267
  %269 = load i64, ptr %42, align 8, !tbaa !10
  %270 = load i64, ptr %9, align 8, !tbaa !10
  %271 = mul i64 %269, %270
  %272 = add i64 %268, %271
  %273 = load i64, ptr %43, align 8, !tbaa !10
  %274 = load i64, ptr %10, align 8, !tbaa !10
  %275 = mul i64 %273, %274
  %276 = add i64 %272, %275
  call void @llvm.lifetime.end.p0(ptr %44) #4
  call void @llvm.lifetime.end.p0(ptr %43) #4
  call void @llvm.lifetime.end.p0(ptr %42) #4
  call void @llvm.lifetime.end.p0(ptr %41) #4
  call void @llvm.lifetime.end.p0(ptr %40) #4
  call void @llvm.lifetime.end.p0(ptr %39) #4
  call void @llvm.lifetime.end.p0(ptr %38) #4
  call void @llvm.lifetime.end.p0(ptr %37) #4
  call void @llvm.lifetime.end.p0(ptr %36) #4
  call void @llvm.lifetime.end.p0(ptr %35) #4
  call void @llvm.lifetime.end.p0(ptr %34) #4
  call void @llvm.lifetime.end.p0(ptr %33) #4
  call void @llvm.lifetime.end.p0(ptr %32) #4
  call void @llvm.lifetime.end.p0(ptr %31) #4
  call void @llvm.lifetime.end.p0(ptr %30) #4
  call void @llvm.lifetime.end.p0(ptr %29) #4
  call void @llvm.lifetime.end.p0(ptr %28) #4
  call void @llvm.lifetime.end.p0(ptr %27) #4
  call void @llvm.lifetime.end.p0(ptr %26) #4
  call void @llvm.lifetime.end.p0(ptr %25) #4
  call void @llvm.lifetime.end.p0(ptr %24) #4
  call void @llvm.lifetime.end.p0(ptr %23) #4
  call void @llvm.lifetime.end.p0(ptr %22) #4
  call void @llvm.lifetime.end.p0(ptr %21) #4
  call void @llvm.lifetime.end.p0(ptr %20) #4
  call void @llvm.lifetime.end.p0(ptr %19) #4
  call void @llvm.lifetime.end.p0(ptr %18) #4
  call void @llvm.lifetime.end.p0(ptr %17) #4
  call void @llvm.lifetime.end.p0(ptr %16) #4
  call void @llvm.lifetime.end.p0(ptr %15) #4
  call void @llvm.lifetime.end.p0(ptr %14) #4
  call void @llvm.lifetime.end.p0(ptr %13) #4
  call void @llvm.lifetime.end.p0(ptr %12) #4
  call void @llvm.lifetime.end.p0(ptr %11) #4
  call void @llvm.lifetime.end.p0(ptr %10) #4
  call void @llvm.lifetime.end.p0(ptr %9) #4
  call void @llvm.lifetime.end.p0(ptr %8) #4
  call void @llvm.lifetime.end.p0(ptr %7) #4
  call void @llvm.lifetime.end.p0(ptr %6) #4
  call void @llvm.lifetime.end.p0(ptr %5) #4
  call void @llvm.lifetime.end.p0(ptr %4) #4
  call void @llvm.lifetime.end.p0(ptr %3) #4
  ret i64 %276
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.va_start.p0(ptr) #2

declare i64 @ext(i64 noundef) #3

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.va_end.p0(ptr) #2

attributes #0 = { noinline nounwind sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { nocallback nofree nosync nounwind willreturn }
attributes #3 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
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
!11 = !{!"long", !8, i64 0}
!12 = distinct !{!12, !13, !14}
!13 = !{!"llvm.loop.mustprogress"}
!14 = !{!"llvm.loop.unroll.disable"}
