; Regression test: functions with more than 256 basic blocks. The backend kept the
; block id in a u8, so the "jmp to the next block" elision fired for the wrong
; blocks and dropped the back edge jmp of the loop below.
; RUN: %foffcc %s %t.o
; RUN: clang %t.o -o %t.out
; RUN: %t.out | FileCheck %s
; CHECK: 5

@fmt = private constant [4 x i8] c"%d\0A\00"
@zero = global i32 0
declare i32 @printf(ptr, ...)

define i32 @count(i32 %n) {
entry:
  br label %b0

b0:
  %v0 = load volatile i32, ptr @zero
  %z0 = icmp eq i32 %v0, 0
  br i1 %z0, label %b1, label %bad

b1:
  %v1 = load volatile i32, ptr @zero
  %z1 = icmp eq i32 %v1, 0
  br i1 %z1, label %b2, label %bad

b2:
  %v2 = load volatile i32, ptr @zero
  %z2 = icmp eq i32 %v2, 0
  br i1 %z2, label %b3, label %bad

b3:
  %v3 = load volatile i32, ptr @zero
  %z3 = icmp eq i32 %v3, 0
  br i1 %z3, label %b4, label %bad

b4:
  %v4 = load volatile i32, ptr @zero
  %z4 = icmp eq i32 %v4, 0
  br i1 %z4, label %b5, label %bad

b5:
  %v5 = load volatile i32, ptr @zero
  %z5 = icmp eq i32 %v5, 0
  br i1 %z5, label %b6, label %bad

b6:
  %v6 = load volatile i32, ptr @zero
  %z6 = icmp eq i32 %v6, 0
  br i1 %z6, label %b7, label %bad

b7:
  %v7 = load volatile i32, ptr @zero
  %z7 = icmp eq i32 %v7, 0
  br i1 %z7, label %b8, label %bad

b8:
  %v8 = load volatile i32, ptr @zero
  %z8 = icmp eq i32 %v8, 0
  br i1 %z8, label %b9, label %bad

b9:
  %v9 = load volatile i32, ptr @zero
  %z9 = icmp eq i32 %v9, 0
  br i1 %z9, label %b10, label %bad

b10:
  %v10 = load volatile i32, ptr @zero
  %z10 = icmp eq i32 %v10, 0
  br i1 %z10, label %b11, label %bad

b11:
  %v11 = load volatile i32, ptr @zero
  %z11 = icmp eq i32 %v11, 0
  br i1 %z11, label %b12, label %bad

b12:
  %v12 = load volatile i32, ptr @zero
  %z12 = icmp eq i32 %v12, 0
  br i1 %z12, label %b13, label %bad

b13:
  %v13 = load volatile i32, ptr @zero
  %z13 = icmp eq i32 %v13, 0
  br i1 %z13, label %b14, label %bad

b14:
  %v14 = load volatile i32, ptr @zero
  %z14 = icmp eq i32 %v14, 0
  br i1 %z14, label %b15, label %bad

b15:
  %v15 = load volatile i32, ptr @zero
  %z15 = icmp eq i32 %v15, 0
  br i1 %z15, label %b16, label %bad

b16:
  %v16 = load volatile i32, ptr @zero
  %z16 = icmp eq i32 %v16, 0
  br i1 %z16, label %b17, label %bad

b17:
  %v17 = load volatile i32, ptr @zero
  %z17 = icmp eq i32 %v17, 0
  br i1 %z17, label %b18, label %bad

b18:
  %v18 = load volatile i32, ptr @zero
  %z18 = icmp eq i32 %v18, 0
  br i1 %z18, label %b19, label %bad

b19:
  %v19 = load volatile i32, ptr @zero
  %z19 = icmp eq i32 %v19, 0
  br i1 %z19, label %b20, label %bad

b20:
  %v20 = load volatile i32, ptr @zero
  %z20 = icmp eq i32 %v20, 0
  br i1 %z20, label %b21, label %bad

b21:
  %v21 = load volatile i32, ptr @zero
  %z21 = icmp eq i32 %v21, 0
  br i1 %z21, label %b22, label %bad

b22:
  %v22 = load volatile i32, ptr @zero
  %z22 = icmp eq i32 %v22, 0
  br i1 %z22, label %b23, label %bad

b23:
  %v23 = load volatile i32, ptr @zero
  %z23 = icmp eq i32 %v23, 0
  br i1 %z23, label %b24, label %bad

b24:
  %v24 = load volatile i32, ptr @zero
  %z24 = icmp eq i32 %v24, 0
  br i1 %z24, label %b25, label %bad

b25:
  %v25 = load volatile i32, ptr @zero
  %z25 = icmp eq i32 %v25, 0
  br i1 %z25, label %b26, label %bad

b26:
  %v26 = load volatile i32, ptr @zero
  %z26 = icmp eq i32 %v26, 0
  br i1 %z26, label %b27, label %bad

b27:
  %v27 = load volatile i32, ptr @zero
  %z27 = icmp eq i32 %v27, 0
  br i1 %z27, label %b28, label %bad

b28:
  %v28 = load volatile i32, ptr @zero
  %z28 = icmp eq i32 %v28, 0
  br i1 %z28, label %b29, label %bad

b29:
  %v29 = load volatile i32, ptr @zero
  %z29 = icmp eq i32 %v29, 0
  br i1 %z29, label %b30, label %bad

b30:
  %v30 = load volatile i32, ptr @zero
  %z30 = icmp eq i32 %v30, 0
  br i1 %z30, label %b31, label %bad

b31:
  %v31 = load volatile i32, ptr @zero
  %z31 = icmp eq i32 %v31, 0
  br i1 %z31, label %b32, label %bad

b32:
  %v32 = load volatile i32, ptr @zero
  %z32 = icmp eq i32 %v32, 0
  br i1 %z32, label %b33, label %bad

b33:
  %v33 = load volatile i32, ptr @zero
  %z33 = icmp eq i32 %v33, 0
  br i1 %z33, label %b34, label %bad

b34:
  %v34 = load volatile i32, ptr @zero
  %z34 = icmp eq i32 %v34, 0
  br i1 %z34, label %b35, label %bad

b35:
  %v35 = load volatile i32, ptr @zero
  %z35 = icmp eq i32 %v35, 0
  br i1 %z35, label %b36, label %bad

b36:
  %v36 = load volatile i32, ptr @zero
  %z36 = icmp eq i32 %v36, 0
  br i1 %z36, label %b37, label %bad

b37:
  %v37 = load volatile i32, ptr @zero
  %z37 = icmp eq i32 %v37, 0
  br i1 %z37, label %b38, label %bad

b38:
  %v38 = load volatile i32, ptr @zero
  %z38 = icmp eq i32 %v38, 0
  br i1 %z38, label %b39, label %bad

b39:
  %v39 = load volatile i32, ptr @zero
  %z39 = icmp eq i32 %v39, 0
  br i1 %z39, label %b40, label %bad

b40:
  %v40 = load volatile i32, ptr @zero
  %z40 = icmp eq i32 %v40, 0
  br i1 %z40, label %b41, label %bad

b41:
  %v41 = load volatile i32, ptr @zero
  %z41 = icmp eq i32 %v41, 0
  br i1 %z41, label %b42, label %bad

b42:
  %v42 = load volatile i32, ptr @zero
  %z42 = icmp eq i32 %v42, 0
  br i1 %z42, label %b43, label %bad

b43:
  %v43 = load volatile i32, ptr @zero
  %z43 = icmp eq i32 %v43, 0
  br i1 %z43, label %b44, label %bad

b44:
  %v44 = load volatile i32, ptr @zero
  %z44 = icmp eq i32 %v44, 0
  br i1 %z44, label %b45, label %bad

b45:
  %v45 = load volatile i32, ptr @zero
  %z45 = icmp eq i32 %v45, 0
  br i1 %z45, label %b46, label %bad

b46:
  %i = phi i32 [ 0, %b45 ], [ %inc, %latch ]
  %v46 = load volatile i32, ptr @zero
  %z46 = icmp eq i32 %v46, 0
  br i1 %z46, label %b47, label %bad

b47:
  %v47 = load volatile i32, ptr @zero
  %z47 = icmp eq i32 %v47, 0
  br i1 %z47, label %b48, label %bad

b48:
  %v48 = load volatile i32, ptr @zero
  %z48 = icmp eq i32 %v48, 0
  br i1 %z48, label %b49, label %bad

b49:
  %v49 = load volatile i32, ptr @zero
  %z49 = icmp eq i32 %v49, 0
  br i1 %z49, label %b50, label %bad

b50:
  %v50 = load volatile i32, ptr @zero
  %z50 = icmp eq i32 %v50, 0
  br i1 %z50, label %b51, label %bad

b51:
  %v51 = load volatile i32, ptr @zero
  %z51 = icmp eq i32 %v51, 0
  br i1 %z51, label %b52, label %bad

b52:
  %v52 = load volatile i32, ptr @zero
  %z52 = icmp eq i32 %v52, 0
  br i1 %z52, label %b53, label %bad

b53:
  %v53 = load volatile i32, ptr @zero
  %z53 = icmp eq i32 %v53, 0
  br i1 %z53, label %b54, label %bad

b54:
  %v54 = load volatile i32, ptr @zero
  %z54 = icmp eq i32 %v54, 0
  br i1 %z54, label %b55, label %bad

b55:
  %v55 = load volatile i32, ptr @zero
  %z55 = icmp eq i32 %v55, 0
  br i1 %z55, label %b56, label %bad

b56:
  %v56 = load volatile i32, ptr @zero
  %z56 = icmp eq i32 %v56, 0
  br i1 %z56, label %b57, label %bad

b57:
  %v57 = load volatile i32, ptr @zero
  %z57 = icmp eq i32 %v57, 0
  br i1 %z57, label %b58, label %bad

b58:
  %v58 = load volatile i32, ptr @zero
  %z58 = icmp eq i32 %v58, 0
  br i1 %z58, label %b59, label %bad

b59:
  %v59 = load volatile i32, ptr @zero
  %z59 = icmp eq i32 %v59, 0
  br i1 %z59, label %b60, label %bad

b60:
  %v60 = load volatile i32, ptr @zero
  %z60 = icmp eq i32 %v60, 0
  br i1 %z60, label %b61, label %bad

b61:
  %v61 = load volatile i32, ptr @zero
  %z61 = icmp eq i32 %v61, 0
  br i1 %z61, label %b62, label %bad

b62:
  %v62 = load volatile i32, ptr @zero
  %z62 = icmp eq i32 %v62, 0
  br i1 %z62, label %b63, label %bad

b63:
  %v63 = load volatile i32, ptr @zero
  %z63 = icmp eq i32 %v63, 0
  br i1 %z63, label %b64, label %bad

b64:
  %v64 = load volatile i32, ptr @zero
  %z64 = icmp eq i32 %v64, 0
  br i1 %z64, label %b65, label %bad

b65:
  %v65 = load volatile i32, ptr @zero
  %z65 = icmp eq i32 %v65, 0
  br i1 %z65, label %b66, label %bad

b66:
  %v66 = load volatile i32, ptr @zero
  %z66 = icmp eq i32 %v66, 0
  br i1 %z66, label %b67, label %bad

b67:
  %v67 = load volatile i32, ptr @zero
  %z67 = icmp eq i32 %v67, 0
  br i1 %z67, label %b68, label %bad

b68:
  %v68 = load volatile i32, ptr @zero
  %z68 = icmp eq i32 %v68, 0
  br i1 %z68, label %b69, label %bad

b69:
  %v69 = load volatile i32, ptr @zero
  %z69 = icmp eq i32 %v69, 0
  br i1 %z69, label %b70, label %bad

b70:
  %v70 = load volatile i32, ptr @zero
  %z70 = icmp eq i32 %v70, 0
  br i1 %z70, label %b71, label %bad

b71:
  %v71 = load volatile i32, ptr @zero
  %z71 = icmp eq i32 %v71, 0
  br i1 %z71, label %b72, label %bad

b72:
  %v72 = load volatile i32, ptr @zero
  %z72 = icmp eq i32 %v72, 0
  br i1 %z72, label %b73, label %bad

b73:
  %v73 = load volatile i32, ptr @zero
  %z73 = icmp eq i32 %v73, 0
  br i1 %z73, label %b74, label %bad

b74:
  %v74 = load volatile i32, ptr @zero
  %z74 = icmp eq i32 %v74, 0
  br i1 %z74, label %b75, label %bad

b75:
  %v75 = load volatile i32, ptr @zero
  %z75 = icmp eq i32 %v75, 0
  br i1 %z75, label %b76, label %bad

b76:
  %v76 = load volatile i32, ptr @zero
  %z76 = icmp eq i32 %v76, 0
  br i1 %z76, label %b77, label %bad

b77:
  %v77 = load volatile i32, ptr @zero
  %z77 = icmp eq i32 %v77, 0
  br i1 %z77, label %b78, label %bad

b78:
  %v78 = load volatile i32, ptr @zero
  %z78 = icmp eq i32 %v78, 0
  br i1 %z78, label %b79, label %bad

b79:
  %v79 = load volatile i32, ptr @zero
  %z79 = icmp eq i32 %v79, 0
  br i1 %z79, label %b80, label %bad

b80:
  %v80 = load volatile i32, ptr @zero
  %z80 = icmp eq i32 %v80, 0
  br i1 %z80, label %b81, label %bad

b81:
  %v81 = load volatile i32, ptr @zero
  %z81 = icmp eq i32 %v81, 0
  br i1 %z81, label %b82, label %bad

b82:
  %v82 = load volatile i32, ptr @zero
  %z82 = icmp eq i32 %v82, 0
  br i1 %z82, label %b83, label %bad

b83:
  %v83 = load volatile i32, ptr @zero
  %z83 = icmp eq i32 %v83, 0
  br i1 %z83, label %b84, label %bad

b84:
  %v84 = load volatile i32, ptr @zero
  %z84 = icmp eq i32 %v84, 0
  br i1 %z84, label %b85, label %bad

b85:
  %v85 = load volatile i32, ptr @zero
  %z85 = icmp eq i32 %v85, 0
  br i1 %z85, label %b86, label %bad

b86:
  %v86 = load volatile i32, ptr @zero
  %z86 = icmp eq i32 %v86, 0
  br i1 %z86, label %b87, label %bad

b87:
  %v87 = load volatile i32, ptr @zero
  %z87 = icmp eq i32 %v87, 0
  br i1 %z87, label %b88, label %bad

b88:
  %v88 = load volatile i32, ptr @zero
  %z88 = icmp eq i32 %v88, 0
  br i1 %z88, label %b89, label %bad

b89:
  %v89 = load volatile i32, ptr @zero
  %z89 = icmp eq i32 %v89, 0
  br i1 %z89, label %b90, label %bad

b90:
  %v90 = load volatile i32, ptr @zero
  %z90 = icmp eq i32 %v90, 0
  br i1 %z90, label %b91, label %bad

b91:
  %v91 = load volatile i32, ptr @zero
  %z91 = icmp eq i32 %v91, 0
  br i1 %z91, label %b92, label %bad

b92:
  %v92 = load volatile i32, ptr @zero
  %z92 = icmp eq i32 %v92, 0
  br i1 %z92, label %b93, label %bad

b93:
  %v93 = load volatile i32, ptr @zero
  %z93 = icmp eq i32 %v93, 0
  br i1 %z93, label %b94, label %bad

b94:
  %v94 = load volatile i32, ptr @zero
  %z94 = icmp eq i32 %v94, 0
  br i1 %z94, label %b95, label %bad

b95:
  %v95 = load volatile i32, ptr @zero
  %z95 = icmp eq i32 %v95, 0
  br i1 %z95, label %b96, label %bad

b96:
  %v96 = load volatile i32, ptr @zero
  %z96 = icmp eq i32 %v96, 0
  br i1 %z96, label %b97, label %bad

b97:
  %v97 = load volatile i32, ptr @zero
  %z97 = icmp eq i32 %v97, 0
  br i1 %z97, label %b98, label %bad

b98:
  %v98 = load volatile i32, ptr @zero
  %z98 = icmp eq i32 %v98, 0
  br i1 %z98, label %b99, label %bad

b99:
  %v99 = load volatile i32, ptr @zero
  %z99 = icmp eq i32 %v99, 0
  br i1 %z99, label %b100, label %bad

b100:
  %v100 = load volatile i32, ptr @zero
  %z100 = icmp eq i32 %v100, 0
  br i1 %z100, label %b101, label %bad

b101:
  %v101 = load volatile i32, ptr @zero
  %z101 = icmp eq i32 %v101, 0
  br i1 %z101, label %b102, label %bad

b102:
  %v102 = load volatile i32, ptr @zero
  %z102 = icmp eq i32 %v102, 0
  br i1 %z102, label %b103, label %bad

b103:
  %v103 = load volatile i32, ptr @zero
  %z103 = icmp eq i32 %v103, 0
  br i1 %z103, label %b104, label %bad

b104:
  %v104 = load volatile i32, ptr @zero
  %z104 = icmp eq i32 %v104, 0
  br i1 %z104, label %b105, label %bad

b105:
  %v105 = load volatile i32, ptr @zero
  %z105 = icmp eq i32 %v105, 0
  br i1 %z105, label %b106, label %bad

b106:
  %v106 = load volatile i32, ptr @zero
  %z106 = icmp eq i32 %v106, 0
  br i1 %z106, label %b107, label %bad

b107:
  %v107 = load volatile i32, ptr @zero
  %z107 = icmp eq i32 %v107, 0
  br i1 %z107, label %b108, label %bad

b108:
  %v108 = load volatile i32, ptr @zero
  %z108 = icmp eq i32 %v108, 0
  br i1 %z108, label %b109, label %bad

b109:
  %v109 = load volatile i32, ptr @zero
  %z109 = icmp eq i32 %v109, 0
  br i1 %z109, label %b110, label %bad

b110:
  %v110 = load volatile i32, ptr @zero
  %z110 = icmp eq i32 %v110, 0
  br i1 %z110, label %b111, label %bad

b111:
  %v111 = load volatile i32, ptr @zero
  %z111 = icmp eq i32 %v111, 0
  br i1 %z111, label %b112, label %bad

b112:
  %v112 = load volatile i32, ptr @zero
  %z112 = icmp eq i32 %v112, 0
  br i1 %z112, label %b113, label %bad

b113:
  %v113 = load volatile i32, ptr @zero
  %z113 = icmp eq i32 %v113, 0
  br i1 %z113, label %b114, label %bad

b114:
  %v114 = load volatile i32, ptr @zero
  %z114 = icmp eq i32 %v114, 0
  br i1 %z114, label %b115, label %bad

b115:
  %v115 = load volatile i32, ptr @zero
  %z115 = icmp eq i32 %v115, 0
  br i1 %z115, label %b116, label %bad

b116:
  %v116 = load volatile i32, ptr @zero
  %z116 = icmp eq i32 %v116, 0
  br i1 %z116, label %b117, label %bad

b117:
  %v117 = load volatile i32, ptr @zero
  %z117 = icmp eq i32 %v117, 0
  br i1 %z117, label %b118, label %bad

b118:
  %v118 = load volatile i32, ptr @zero
  %z118 = icmp eq i32 %v118, 0
  br i1 %z118, label %b119, label %bad

b119:
  %v119 = load volatile i32, ptr @zero
  %z119 = icmp eq i32 %v119, 0
  br i1 %z119, label %b120, label %bad

b120:
  %v120 = load volatile i32, ptr @zero
  %z120 = icmp eq i32 %v120, 0
  br i1 %z120, label %b121, label %bad

b121:
  %v121 = load volatile i32, ptr @zero
  %z121 = icmp eq i32 %v121, 0
  br i1 %z121, label %b122, label %bad

b122:
  %v122 = load volatile i32, ptr @zero
  %z122 = icmp eq i32 %v122, 0
  br i1 %z122, label %b123, label %bad

b123:
  %v123 = load volatile i32, ptr @zero
  %z123 = icmp eq i32 %v123, 0
  br i1 %z123, label %b124, label %bad

b124:
  %v124 = load volatile i32, ptr @zero
  %z124 = icmp eq i32 %v124, 0
  br i1 %z124, label %b125, label %bad

b125:
  %v125 = load volatile i32, ptr @zero
  %z125 = icmp eq i32 %v125, 0
  br i1 %z125, label %b126, label %bad

b126:
  %v126 = load volatile i32, ptr @zero
  %z126 = icmp eq i32 %v126, 0
  br i1 %z126, label %b127, label %bad

b127:
  %v127 = load volatile i32, ptr @zero
  %z127 = icmp eq i32 %v127, 0
  br i1 %z127, label %b128, label %bad

b128:
  %v128 = load volatile i32, ptr @zero
  %z128 = icmp eq i32 %v128, 0
  br i1 %z128, label %b129, label %bad

b129:
  %v129 = load volatile i32, ptr @zero
  %z129 = icmp eq i32 %v129, 0
  br i1 %z129, label %b130, label %bad

b130:
  %v130 = load volatile i32, ptr @zero
  %z130 = icmp eq i32 %v130, 0
  br i1 %z130, label %b131, label %bad

b131:
  %v131 = load volatile i32, ptr @zero
  %z131 = icmp eq i32 %v131, 0
  br i1 %z131, label %b132, label %bad

b132:
  %v132 = load volatile i32, ptr @zero
  %z132 = icmp eq i32 %v132, 0
  br i1 %z132, label %b133, label %bad

b133:
  %v133 = load volatile i32, ptr @zero
  %z133 = icmp eq i32 %v133, 0
  br i1 %z133, label %b134, label %bad

b134:
  %v134 = load volatile i32, ptr @zero
  %z134 = icmp eq i32 %v134, 0
  br i1 %z134, label %b135, label %bad

b135:
  %v135 = load volatile i32, ptr @zero
  %z135 = icmp eq i32 %v135, 0
  br i1 %z135, label %b136, label %bad

b136:
  %v136 = load volatile i32, ptr @zero
  %z136 = icmp eq i32 %v136, 0
  br i1 %z136, label %b137, label %bad

b137:
  %v137 = load volatile i32, ptr @zero
  %z137 = icmp eq i32 %v137, 0
  br i1 %z137, label %b138, label %bad

b138:
  %v138 = load volatile i32, ptr @zero
  %z138 = icmp eq i32 %v138, 0
  br i1 %z138, label %b139, label %bad

b139:
  %v139 = load volatile i32, ptr @zero
  %z139 = icmp eq i32 %v139, 0
  br i1 %z139, label %b140, label %bad

b140:
  %v140 = load volatile i32, ptr @zero
  %z140 = icmp eq i32 %v140, 0
  br i1 %z140, label %b141, label %bad

b141:
  %v141 = load volatile i32, ptr @zero
  %z141 = icmp eq i32 %v141, 0
  br i1 %z141, label %b142, label %bad

b142:
  %v142 = load volatile i32, ptr @zero
  %z142 = icmp eq i32 %v142, 0
  br i1 %z142, label %b143, label %bad

b143:
  %v143 = load volatile i32, ptr @zero
  %z143 = icmp eq i32 %v143, 0
  br i1 %z143, label %b144, label %bad

b144:
  %v144 = load volatile i32, ptr @zero
  %z144 = icmp eq i32 %v144, 0
  br i1 %z144, label %b145, label %bad

b145:
  %v145 = load volatile i32, ptr @zero
  %z145 = icmp eq i32 %v145, 0
  br i1 %z145, label %b146, label %bad

b146:
  %v146 = load volatile i32, ptr @zero
  %z146 = icmp eq i32 %v146, 0
  br i1 %z146, label %b147, label %bad

b147:
  %v147 = load volatile i32, ptr @zero
  %z147 = icmp eq i32 %v147, 0
  br i1 %z147, label %b148, label %bad

b148:
  %v148 = load volatile i32, ptr @zero
  %z148 = icmp eq i32 %v148, 0
  br i1 %z148, label %b149, label %bad

b149:
  %v149 = load volatile i32, ptr @zero
  %z149 = icmp eq i32 %v149, 0
  br i1 %z149, label %b150, label %bad

b150:
  %v150 = load volatile i32, ptr @zero
  %z150 = icmp eq i32 %v150, 0
  br i1 %z150, label %b151, label %bad

b151:
  %v151 = load volatile i32, ptr @zero
  %z151 = icmp eq i32 %v151, 0
  br i1 %z151, label %b152, label %bad

b152:
  %v152 = load volatile i32, ptr @zero
  %z152 = icmp eq i32 %v152, 0
  br i1 %z152, label %b153, label %bad

b153:
  %v153 = load volatile i32, ptr @zero
  %z153 = icmp eq i32 %v153, 0
  br i1 %z153, label %b154, label %bad

b154:
  %v154 = load volatile i32, ptr @zero
  %z154 = icmp eq i32 %v154, 0
  br i1 %z154, label %b155, label %bad

b155:
  %v155 = load volatile i32, ptr @zero
  %z155 = icmp eq i32 %v155, 0
  br i1 %z155, label %b156, label %bad

b156:
  %v156 = load volatile i32, ptr @zero
  %z156 = icmp eq i32 %v156, 0
  br i1 %z156, label %b157, label %bad

b157:
  %v157 = load volatile i32, ptr @zero
  %z157 = icmp eq i32 %v157, 0
  br i1 %z157, label %b158, label %bad

b158:
  %v158 = load volatile i32, ptr @zero
  %z158 = icmp eq i32 %v158, 0
  br i1 %z158, label %b159, label %bad

b159:
  %v159 = load volatile i32, ptr @zero
  %z159 = icmp eq i32 %v159, 0
  br i1 %z159, label %b160, label %bad

b160:
  %v160 = load volatile i32, ptr @zero
  %z160 = icmp eq i32 %v160, 0
  br i1 %z160, label %b161, label %bad

b161:
  %v161 = load volatile i32, ptr @zero
  %z161 = icmp eq i32 %v161, 0
  br i1 %z161, label %b162, label %bad

b162:
  %v162 = load volatile i32, ptr @zero
  %z162 = icmp eq i32 %v162, 0
  br i1 %z162, label %b163, label %bad

b163:
  %v163 = load volatile i32, ptr @zero
  %z163 = icmp eq i32 %v163, 0
  br i1 %z163, label %b164, label %bad

b164:
  %v164 = load volatile i32, ptr @zero
  %z164 = icmp eq i32 %v164, 0
  br i1 %z164, label %b165, label %bad

b165:
  %v165 = load volatile i32, ptr @zero
  %z165 = icmp eq i32 %v165, 0
  br i1 %z165, label %b166, label %bad

b166:
  %v166 = load volatile i32, ptr @zero
  %z166 = icmp eq i32 %v166, 0
  br i1 %z166, label %b167, label %bad

b167:
  %v167 = load volatile i32, ptr @zero
  %z167 = icmp eq i32 %v167, 0
  br i1 %z167, label %b168, label %bad

b168:
  %v168 = load volatile i32, ptr @zero
  %z168 = icmp eq i32 %v168, 0
  br i1 %z168, label %b169, label %bad

b169:
  %v169 = load volatile i32, ptr @zero
  %z169 = icmp eq i32 %v169, 0
  br i1 %z169, label %b170, label %bad

b170:
  %v170 = load volatile i32, ptr @zero
  %z170 = icmp eq i32 %v170, 0
  br i1 %z170, label %b171, label %bad

b171:
  %v171 = load volatile i32, ptr @zero
  %z171 = icmp eq i32 %v171, 0
  br i1 %z171, label %b172, label %bad

b172:
  %v172 = load volatile i32, ptr @zero
  %z172 = icmp eq i32 %v172, 0
  br i1 %z172, label %b173, label %bad

b173:
  %v173 = load volatile i32, ptr @zero
  %z173 = icmp eq i32 %v173, 0
  br i1 %z173, label %b174, label %bad

b174:
  %v174 = load volatile i32, ptr @zero
  %z174 = icmp eq i32 %v174, 0
  br i1 %z174, label %b175, label %bad

b175:
  %v175 = load volatile i32, ptr @zero
  %z175 = icmp eq i32 %v175, 0
  br i1 %z175, label %b176, label %bad

b176:
  %v176 = load volatile i32, ptr @zero
  %z176 = icmp eq i32 %v176, 0
  br i1 %z176, label %b177, label %bad

b177:
  %v177 = load volatile i32, ptr @zero
  %z177 = icmp eq i32 %v177, 0
  br i1 %z177, label %b178, label %bad

b178:
  %v178 = load volatile i32, ptr @zero
  %z178 = icmp eq i32 %v178, 0
  br i1 %z178, label %b179, label %bad

b179:
  %v179 = load volatile i32, ptr @zero
  %z179 = icmp eq i32 %v179, 0
  br i1 %z179, label %b180, label %bad

b180:
  %v180 = load volatile i32, ptr @zero
  %z180 = icmp eq i32 %v180, 0
  br i1 %z180, label %b181, label %bad

b181:
  %v181 = load volatile i32, ptr @zero
  %z181 = icmp eq i32 %v181, 0
  br i1 %z181, label %b182, label %bad

b182:
  %v182 = load volatile i32, ptr @zero
  %z182 = icmp eq i32 %v182, 0
  br i1 %z182, label %b183, label %bad

b183:
  %v183 = load volatile i32, ptr @zero
  %z183 = icmp eq i32 %v183, 0
  br i1 %z183, label %b184, label %bad

b184:
  %v184 = load volatile i32, ptr @zero
  %z184 = icmp eq i32 %v184, 0
  br i1 %z184, label %b185, label %bad

b185:
  %v185 = load volatile i32, ptr @zero
  %z185 = icmp eq i32 %v185, 0
  br i1 %z185, label %b186, label %bad

b186:
  %v186 = load volatile i32, ptr @zero
  %z186 = icmp eq i32 %v186, 0
  br i1 %z186, label %b187, label %bad

b187:
  %v187 = load volatile i32, ptr @zero
  %z187 = icmp eq i32 %v187, 0
  br i1 %z187, label %b188, label %bad

b188:
  %v188 = load volatile i32, ptr @zero
  %z188 = icmp eq i32 %v188, 0
  br i1 %z188, label %b189, label %bad

b189:
  %v189 = load volatile i32, ptr @zero
  %z189 = icmp eq i32 %v189, 0
  br i1 %z189, label %b190, label %bad

b190:
  %v190 = load volatile i32, ptr @zero
  %z190 = icmp eq i32 %v190, 0
  br i1 %z190, label %b191, label %bad

b191:
  %v191 = load volatile i32, ptr @zero
  %z191 = icmp eq i32 %v191, 0
  br i1 %z191, label %b192, label %bad

b192:
  %v192 = load volatile i32, ptr @zero
  %z192 = icmp eq i32 %v192, 0
  br i1 %z192, label %b193, label %bad

b193:
  %v193 = load volatile i32, ptr @zero
  %z193 = icmp eq i32 %v193, 0
  br i1 %z193, label %b194, label %bad

b194:
  %v194 = load volatile i32, ptr @zero
  %z194 = icmp eq i32 %v194, 0
  br i1 %z194, label %b195, label %bad

b195:
  %v195 = load volatile i32, ptr @zero
  %z195 = icmp eq i32 %v195, 0
  br i1 %z195, label %b196, label %bad

b196:
  %v196 = load volatile i32, ptr @zero
  %z196 = icmp eq i32 %v196, 0
  br i1 %z196, label %b197, label %bad

b197:
  %v197 = load volatile i32, ptr @zero
  %z197 = icmp eq i32 %v197, 0
  br i1 %z197, label %b198, label %bad

b198:
  %v198 = load volatile i32, ptr @zero
  %z198 = icmp eq i32 %v198, 0
  br i1 %z198, label %b199, label %bad

b199:
  %v199 = load volatile i32, ptr @zero
  %z199 = icmp eq i32 %v199, 0
  br i1 %z199, label %b200, label %bad

b200:
  %v200 = load volatile i32, ptr @zero
  %z200 = icmp eq i32 %v200, 0
  br i1 %z200, label %b201, label %bad

b201:
  %v201 = load volatile i32, ptr @zero
  %z201 = icmp eq i32 %v201, 0
  br i1 %z201, label %b202, label %bad

b202:
  %v202 = load volatile i32, ptr @zero
  %z202 = icmp eq i32 %v202, 0
  br i1 %z202, label %b203, label %bad

b203:
  %v203 = load volatile i32, ptr @zero
  %z203 = icmp eq i32 %v203, 0
  br i1 %z203, label %b204, label %bad

b204:
  %v204 = load volatile i32, ptr @zero
  %z204 = icmp eq i32 %v204, 0
  br i1 %z204, label %b205, label %bad

b205:
  %v205 = load volatile i32, ptr @zero
  %z205 = icmp eq i32 %v205, 0
  br i1 %z205, label %b206, label %bad

b206:
  %v206 = load volatile i32, ptr @zero
  %z206 = icmp eq i32 %v206, 0
  br i1 %z206, label %b207, label %bad

b207:
  %v207 = load volatile i32, ptr @zero
  %z207 = icmp eq i32 %v207, 0
  br i1 %z207, label %b208, label %bad

b208:
  %v208 = load volatile i32, ptr @zero
  %z208 = icmp eq i32 %v208, 0
  br i1 %z208, label %b209, label %bad

b209:
  %v209 = load volatile i32, ptr @zero
  %z209 = icmp eq i32 %v209, 0
  br i1 %z209, label %b210, label %bad

b210:
  %v210 = load volatile i32, ptr @zero
  %z210 = icmp eq i32 %v210, 0
  br i1 %z210, label %b211, label %bad

b211:
  %v211 = load volatile i32, ptr @zero
  %z211 = icmp eq i32 %v211, 0
  br i1 %z211, label %b212, label %bad

b212:
  %v212 = load volatile i32, ptr @zero
  %z212 = icmp eq i32 %v212, 0
  br i1 %z212, label %b213, label %bad

b213:
  %v213 = load volatile i32, ptr @zero
  %z213 = icmp eq i32 %v213, 0
  br i1 %z213, label %b214, label %bad

b214:
  %v214 = load volatile i32, ptr @zero
  %z214 = icmp eq i32 %v214, 0
  br i1 %z214, label %b215, label %bad

b215:
  %v215 = load volatile i32, ptr @zero
  %z215 = icmp eq i32 %v215, 0
  br i1 %z215, label %b216, label %bad

b216:
  %v216 = load volatile i32, ptr @zero
  %z216 = icmp eq i32 %v216, 0
  br i1 %z216, label %b217, label %bad

b217:
  %v217 = load volatile i32, ptr @zero
  %z217 = icmp eq i32 %v217, 0
  br i1 %z217, label %b218, label %bad

b218:
  %v218 = load volatile i32, ptr @zero
  %z218 = icmp eq i32 %v218, 0
  br i1 %z218, label %b219, label %bad

b219:
  %v219 = load volatile i32, ptr @zero
  %z219 = icmp eq i32 %v219, 0
  br i1 %z219, label %b220, label %bad

b220:
  %v220 = load volatile i32, ptr @zero
  %z220 = icmp eq i32 %v220, 0
  br i1 %z220, label %b221, label %bad

b221:
  %v221 = load volatile i32, ptr @zero
  %z221 = icmp eq i32 %v221, 0
  br i1 %z221, label %b222, label %bad

b222:
  %v222 = load volatile i32, ptr @zero
  %z222 = icmp eq i32 %v222, 0
  br i1 %z222, label %b223, label %bad

b223:
  %v223 = load volatile i32, ptr @zero
  %z223 = icmp eq i32 %v223, 0
  br i1 %z223, label %b224, label %bad

b224:
  %v224 = load volatile i32, ptr @zero
  %z224 = icmp eq i32 %v224, 0
  br i1 %z224, label %b225, label %bad

b225:
  %v225 = load volatile i32, ptr @zero
  %z225 = icmp eq i32 %v225, 0
  br i1 %z225, label %b226, label %bad

b226:
  %v226 = load volatile i32, ptr @zero
  %z226 = icmp eq i32 %v226, 0
  br i1 %z226, label %b227, label %bad

b227:
  %v227 = load volatile i32, ptr @zero
  %z227 = icmp eq i32 %v227, 0
  br i1 %z227, label %b228, label %bad

b228:
  %v228 = load volatile i32, ptr @zero
  %z228 = icmp eq i32 %v228, 0
  br i1 %z228, label %b229, label %bad

b229:
  %v229 = load volatile i32, ptr @zero
  %z229 = icmp eq i32 %v229, 0
  br i1 %z229, label %b230, label %bad

b230:
  %v230 = load volatile i32, ptr @zero
  %z230 = icmp eq i32 %v230, 0
  br i1 %z230, label %b231, label %bad

b231:
  %v231 = load volatile i32, ptr @zero
  %z231 = icmp eq i32 %v231, 0
  br i1 %z231, label %b232, label %bad

b232:
  %v232 = load volatile i32, ptr @zero
  %z232 = icmp eq i32 %v232, 0
  br i1 %z232, label %b233, label %bad

b233:
  %v233 = load volatile i32, ptr @zero
  %z233 = icmp eq i32 %v233, 0
  br i1 %z233, label %b234, label %bad

b234:
  %v234 = load volatile i32, ptr @zero
  %z234 = icmp eq i32 %v234, 0
  br i1 %z234, label %b235, label %bad

b235:
  %v235 = load volatile i32, ptr @zero
  %z235 = icmp eq i32 %v235, 0
  br i1 %z235, label %b236, label %bad

b236:
  %v236 = load volatile i32, ptr @zero
  %z236 = icmp eq i32 %v236, 0
  br i1 %z236, label %b237, label %bad

b237:
  %v237 = load volatile i32, ptr @zero
  %z237 = icmp eq i32 %v237, 0
  br i1 %z237, label %b238, label %bad

b238:
  %v238 = load volatile i32, ptr @zero
  %z238 = icmp eq i32 %v238, 0
  br i1 %z238, label %b239, label %bad

b239:
  %v239 = load volatile i32, ptr @zero
  %z239 = icmp eq i32 %v239, 0
  br i1 %z239, label %b240, label %bad

b240:
  %v240 = load volatile i32, ptr @zero
  %z240 = icmp eq i32 %v240, 0
  br i1 %z240, label %b241, label %bad

b241:
  %v241 = load volatile i32, ptr @zero
  %z241 = icmp eq i32 %v241, 0
  br i1 %z241, label %b242, label %bad

b242:
  %v242 = load volatile i32, ptr @zero
  %z242 = icmp eq i32 %v242, 0
  br i1 %z242, label %b243, label %bad

b243:
  %v243 = load volatile i32, ptr @zero
  %z243 = icmp eq i32 %v243, 0
  br i1 %z243, label %b244, label %bad

b244:
  %v244 = load volatile i32, ptr @zero
  %z244 = icmp eq i32 %v244, 0
  br i1 %z244, label %b245, label %bad

b245:
  %v245 = load volatile i32, ptr @zero
  %z245 = icmp eq i32 %v245, 0
  br i1 %z245, label %b246, label %bad

b246:
  %v246 = load volatile i32, ptr @zero
  %z246 = icmp eq i32 %v246, 0
  br i1 %z246, label %b247, label %bad

b247:
  %v247 = load volatile i32, ptr @zero
  %z247 = icmp eq i32 %v247, 0
  br i1 %z247, label %b248, label %bad

b248:
  %v248 = load volatile i32, ptr @zero
  %z248 = icmp eq i32 %v248, 0
  br i1 %z248, label %b249, label %bad

b249:
  %v249 = load volatile i32, ptr @zero
  %z249 = icmp eq i32 %v249, 0
  br i1 %z249, label %b250, label %bad

b250:
  %v250 = load volatile i32, ptr @zero
  %z250 = icmp eq i32 %v250, 0
  br i1 %z250, label %b251, label %bad

b251:
  %v251 = load volatile i32, ptr @zero
  %z251 = icmp eq i32 %v251, 0
  br i1 %z251, label %b252, label %bad

b252:
  %v252 = load volatile i32, ptr @zero
  %z252 = icmp eq i32 %v252, 0
  br i1 %z252, label %b253, label %bad

b253:
  %v253 = load volatile i32, ptr @zero
  %z253 = icmp eq i32 %v253, 0
  br i1 %z253, label %b254, label %bad

b254:
  %v254 = load volatile i32, ptr @zero
  %z254 = icmp eq i32 %v254, 0
  br i1 %z254, label %b255, label %bad

b255:
  %v255 = load volatile i32, ptr @zero
  %z255 = icmp eq i32 %v255, 0
  br i1 %z255, label %b256, label %bad

b256:
  %v256 = load volatile i32, ptr @zero
  %z256 = icmp eq i32 %v256, 0
  br i1 %z256, label %b257, label %bad

b257:
  %v257 = load volatile i32, ptr @zero
  %z257 = icmp eq i32 %v257, 0
  br i1 %z257, label %b258, label %bad

b258:
  %v258 = load volatile i32, ptr @zero
  %z258 = icmp eq i32 %v258, 0
  br i1 %z258, label %b259, label %bad

b259:
  %v259 = load volatile i32, ptr @zero
  %z259 = icmp eq i32 %v259, 0
  br i1 %z259, label %b260, label %bad

b260:
  %v260 = load volatile i32, ptr @zero
  %z260 = icmp eq i32 %v260, 0
  br i1 %z260, label %b261, label %bad

b261:
  %v261 = load volatile i32, ptr @zero
  %z261 = icmp eq i32 %v261, 0
  br i1 %z261, label %b262, label %bad

b262:
  %v262 = load volatile i32, ptr @zero
  %z262 = icmp eq i32 %v262, 0
  br i1 %z262, label %b263, label %bad

b263:
  %v263 = load volatile i32, ptr @zero
  %z263 = icmp eq i32 %v263, 0
  br i1 %z263, label %b264, label %bad

b264:
  %v264 = load volatile i32, ptr @zero
  %z264 = icmp eq i32 %v264, 0
  br i1 %z264, label %b265, label %bad

b265:
  %v265 = load volatile i32, ptr @zero
  %z265 = icmp eq i32 %v265, 0
  br i1 %z265, label %b266, label %bad

b266:
  %v266 = load volatile i32, ptr @zero
  %z266 = icmp eq i32 %v266, 0
  br i1 %z266, label %b267, label %bad

b267:
  %v267 = load volatile i32, ptr @zero
  %z267 = icmp eq i32 %v267, 0
  br i1 %z267, label %b268, label %bad

b268:
  %v268 = load volatile i32, ptr @zero
  %z268 = icmp eq i32 %v268, 0
  br i1 %z268, label %b269, label %bad

b269:
  %v269 = load volatile i32, ptr @zero
  %z269 = icmp eq i32 %v269, 0
  br i1 %z269, label %b270, label %bad

b270:
  %v270 = load volatile i32, ptr @zero
  %z270 = icmp eq i32 %v270, 0
  br i1 %z270, label %b271, label %bad

b271:
  %v271 = load volatile i32, ptr @zero
  %z271 = icmp eq i32 %v271, 0
  br i1 %z271, label %b272, label %bad

b272:
  %v272 = load volatile i32, ptr @zero
  %z272 = icmp eq i32 %v272, 0
  br i1 %z272, label %b273, label %bad

b273:
  %v273 = load volatile i32, ptr @zero
  %z273 = icmp eq i32 %v273, 0
  br i1 %z273, label %b274, label %bad

b274:
  %v274 = load volatile i32, ptr @zero
  %z274 = icmp eq i32 %v274, 0
  br i1 %z274, label %b275, label %bad

b275:
  %v275 = load volatile i32, ptr @zero
  %z275 = icmp eq i32 %v275, 0
  br i1 %z275, label %b276, label %bad

b276:
  %v276 = load volatile i32, ptr @zero
  %z276 = icmp eq i32 %v276, 0
  br i1 %z276, label %b277, label %bad

b277:
  %v277 = load volatile i32, ptr @zero
  %z277 = icmp eq i32 %v277, 0
  br i1 %z277, label %b278, label %bad

b278:
  %v278 = load volatile i32, ptr @zero
  %z278 = icmp eq i32 %v278, 0
  br i1 %z278, label %b279, label %bad

b279:
  %v279 = load volatile i32, ptr @zero
  %z279 = icmp eq i32 %v279, 0
  br i1 %z279, label %b280, label %bad

b280:
  %v280 = load volatile i32, ptr @zero
  %z280 = icmp eq i32 %v280, 0
  br i1 %z280, label %b281, label %bad

b281:
  %v281 = load volatile i32, ptr @zero
  %z281 = icmp eq i32 %v281, 0
  br i1 %z281, label %b282, label %bad

b282:
  %v282 = load volatile i32, ptr @zero
  %z282 = icmp eq i32 %v282, 0
  br i1 %z282, label %b283, label %bad

b283:
  %v283 = load volatile i32, ptr @zero
  %z283 = icmp eq i32 %v283, 0
  br i1 %z283, label %b284, label %bad

b284:
  %v284 = load volatile i32, ptr @zero
  %z284 = icmp eq i32 %v284, 0
  br i1 %z284, label %b285, label %bad

b285:
  %v285 = load volatile i32, ptr @zero
  %z285 = icmp eq i32 %v285, 0
  br i1 %z285, label %b286, label %bad

b286:
  %v286 = load volatile i32, ptr @zero
  %z286 = icmp eq i32 %v286, 0
  br i1 %z286, label %b287, label %bad

b287:
  %v287 = load volatile i32, ptr @zero
  %z287 = icmp eq i32 %v287, 0
  br i1 %z287, label %b288, label %bad

b288:
  %v288 = load volatile i32, ptr @zero
  %z288 = icmp eq i32 %v288, 0
  br i1 %z288, label %b289, label %bad

b289:
  %v289 = load volatile i32, ptr @zero
  %z289 = icmp eq i32 %v289, 0
  br i1 %z289, label %b290, label %bad

b290:
  %v290 = load volatile i32, ptr @zero
  %z290 = icmp eq i32 %v290, 0
  br i1 %z290, label %b291, label %bad

b291:
  %v291 = load volatile i32, ptr @zero
  %z291 = icmp eq i32 %v291, 0
  br i1 %z291, label %b292, label %bad

b292:
  %v292 = load volatile i32, ptr @zero
  %z292 = icmp eq i32 %v292, 0
  br i1 %z292, label %b293, label %bad

b293:
  %v293 = load volatile i32, ptr @zero
  %z293 = icmp eq i32 %v293, 0
  br i1 %z293, label %b294, label %bad

b294:
  %v294 = load volatile i32, ptr @zero
  %z294 = icmp eq i32 %v294, 0
  br i1 %z294, label %b295, label %bad

b295:
  %v295 = load volatile i32, ptr @zero
  %z295 = icmp eq i32 %v295, 0
  br i1 %z295, label %b296, label %bad

b296:
  %v296 = load volatile i32, ptr @zero
  %z296 = icmp eq i32 %v296, 0
  br i1 %z296, label %b297, label %bad

b297:
  %v297 = load volatile i32, ptr @zero
  %z297 = icmp eq i32 %v297, 0
  br i1 %z297, label %b298, label %bad

b298:
  %v298 = load volatile i32, ptr @zero
  %z298 = icmp eq i32 %v298, 0
  br i1 %z298, label %b299, label %bad

b299:
  %v299 = load volatile i32, ptr @zero
  %z299 = icmp eq i32 %v299, 0
  %inc = add i32 %i, 1
  %c = icmp sge i32 %inc, %n
  br i1 %z299, label %latch, label %bad
latch:
  br i1 %c, label %exit, label %b46

bad:
  ret i32 -1
exit:
  ret i32 %inc
}

define i32 @main() {
  %r = call i32 @count(i32 5)
  %p = call i32 (ptr, ...) @printf(ptr @fmt, i32 %r)
  ret i32 0
}

