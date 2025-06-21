
Simple optimizing backend





## TODO
+ replace cond + select with specific cmovXX commands
	+ missing for floatingpoint
+ optimize llvm intrin lowering like memset/memcopy
+ verification
+ serialize
+ deserialize
+ Abstract away graph based matcher (maybe a DSL)
+ convert codegen into library
+ Dont generate stack setup destruction if no stack usage
	(need to make sure stack is still aligned iff thers call)
+ replace globals only used in 1 func to local variable(iff not recursive)
+ More Passes
	+ Fix epath pre
	+ rework sccp
+ More Tests
	+ GCC Tests




######


valgrind --tool=callgrind --dump-instr=yes --branch-sim=yes --cache-sim=yes --simulate-wb=yes --simulate-hwpref=yes --cacheuse=yes  ./a.out
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=mold" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++     

