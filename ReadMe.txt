
Simple optimizing backend





## TODO

+ verification
+ serialize
+ deserialize
+ somewhat generic dataflow analysis
+ Abstract away graph based matcher (maybe a DSL)
+ convert codegen into library
+ Dont generate stack setup destruction if no stack usage
	(need to make sure stack is still aligned iff thers call)

+ More Passes
	+ Change lifetime check in reg alloc to better handle arguments(only check if they alive after?)
	+ Fix epath pre
	+ rework sccp
	+ add Useable to Globals and Functions
+ More Tests
	+ GCC Tests




######


valgrind --tool=callgrind --dump-instr=yes --branch-sim=yes --cache-sim=yes --simulate-wb=yes --simulate-hwpref=yes --cacheuse=yes  ./a.out
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=mold" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++     

