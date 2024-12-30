
Simple optimizing backend





## TODO

+ verification
+ serialize
+ deserialize
+ somewhat generic dataflow analysis
+ Abstract away graph based matcher (maybe a DSL)
+ convert codegen into library

+ More Passes
	+ Fix epath pre
	+ rework sccp
	+ add Useable to Globals and Functions
	+ improve second live variable analysis
+ More Tests
	+ GCC Tests




######


valgrind --tool=callgrind --dump-instr=yes --branch-sim=yes --cache-sim=yes --simulate-wb=yes --simulate-hwpref=yes --cacheuse=yes  ./a.out
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=mold" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++     

