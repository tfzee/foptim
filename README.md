# Foptim

A relatively simple optimizing compiler backend.

Currently the only frontend takes LLVM IR as input and compiles it down to a native elf object file through several intermediate stages.

## Pipeline

**1. Parsing & IR Lowering**
Ingests LLVM IR and converts it to Foptim's SSA style internal representation (FIR).
Similar to LLVM-IR but simplified and replacing for example PHI nodes with Basicblock arguments.

**2. Optimization**
FIR is processed by a series of function and module passes. Function passes run in parallel while module passes are currently only partially parallelized.

**3. Instruction Selection**
Tree matching is applied to FIR to select assembly like instructions, producing MIR.

**4. MIR Optimization & Legalization & CC & Reg Alloc**
MIR is further optimized and legalized.
Then transforms call arguments to conform to the calling convention, performs basic register allocation, then finalizes CC lowering.

**6. Code Emission**
Final cleanup and output to a elf object file.
Which then can be linked like normal.

## Status

> [!CAUTION]
> Currently targets **x86-64 only**. Some features are on purpose left out like LLVM style exception handling.
> And many features are still missing and correctness is not guaranteed.

## Build instructions
Either use the flake or need to install the minimal dependencies manually

| Dependency | For What |
| --- | --- |
| elfio | for outputting the elf obj file |
| fmt | for printing | 
| argparse | for parsing command line arguments | 
| gtest | for a few unit tests | 
| clang+llvm | optional if using the foptim_main to load in llvmir | 
| ninja | optional for building| 
| ... maybe missing some | .... | 

I personally use these commands for building
```
mkdir build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++
ninja
# Generate llvmir file from a c file use 01-03 with disable llvm optzns to enable frontend optimizations
clang -O1 -mllvm -disable-llvm-optzns -fno-exceptions test.c -o test.ll -S -emit-llvm 
# Generate the object file via foptim 
./foptim_main ./test.ll ./test.o
#link the result
clang ./test.o ./test.out
```

> [!TIP]
> Theres also a driver.sh script to simplify it and make it more like using clang. Also other scripts for specific usages in the scripts folder.


## Tests
> [!IMPORTANT]
> Not included, cause they kinda thrown together and i dont know all the licenses/sources anymore.

The tests are all taken from public sources or written by myself.
Mostly taken from
+ GCCs github repo
+ LLVMs github repo
+ embench github repo
+ fujitsu/compiler-test-suite github repo
+ CppBenchmark github repo
+ Some handwritten

Currently failing 85 out of 473 tests.
These are mostly cause I dont have register spilling implemented.
This is cause I feel like nearly all of them should run without it if optimized good enough. And so I can see progress better.


## Bench
Some bench results and my perfomance overtime. The cases where I am faster might be caused by illegal beheaviour in my Compiler and not superior optimizations.
The last regression in perfomance was from the removal of the current SLP pass to add a newer one, and maybe some other fixes causing slowdowns.

<img width="5006" height="3568" alt="bench_result" src="https://github.com/user-attachments/assets/0a381f19-a658-4f86-9cf7-5a73c76c77d8" />


## TODO
+ Compiler Stuff
	+ make flake.nix to actually be useful for building fully
	+ no register spilling rn
	+ Parallelize Module Passes
		+ inlining
		+ IPCP
		+ argpromotion
		+ funcprop annotator
	+ Support more CCs
	+ Abstract away graph based matcher (maybe a DSL)
		+ Could be used in general like LLVMs matcher stuff in optimization passes
	+ replace std::hash with the ankerl version + replace std::map/set with ankerl
	+ add a custom frontend other then llvmir
	+ serialize
	+ deserialize
+ Optimization Stuff
	+ backwards constant prop (when transformation is applied to for example a buffer constant prop it backwards so its already applied to the constant values)
		+ sometimes already done in trivial cases
	+ replace cond + select with specific cmovXX commands
		+ missing for floatingpoint
	+ 2 allocas without overlapping lifetimes could be merged to use same stack space
	+ optimize sdiv
		+ can convert into imul shifts and add (theres some formular for this)
		+ only have some dumb cases
	+ Add a proper analysis caching system 
	+ More Passes
		+ Get SSAPRE
		+ Rework sccp

