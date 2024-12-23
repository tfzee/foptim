
Simple optimizing backend



valgrind --tool=callgrind --dump-instr=yes --branch-sim=yes --cache-sim=yes --simulate-wb=yes --simulate-hwpref=yes --cacheuse=yes  ./a.out



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
