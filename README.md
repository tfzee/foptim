# Foptim

A relatively simple optimizing compiler backend.

Currently the only frontend takes LLVM IR as input and compiles it down to a native elf object file through several intermediate stages.

## Pipeline

**1. Parsing & IR Lowering**
Ingests LLVM IR and converts it to Foptim's SSA style internal representation (FIR).
Similar to LLVM-IR but simplified and replacing for example PHI nodes with Basicblock arguments.

**2. Optimization**
FIR is processed by a series of function and module passes. Function passes run in parallel while module passes are currently only partially parallelized. (if number of workers > 0)

**3. Instruction Selection**
Tree matching is applied to FIR to select assembly like instructions, producing MIR.

**4. MIR Optimization & Legalization & CC & Reg Alloc**
MIR is further optimized and legalized.
Then transforms call arguments to conform to the calling convention, performs register allocation, then finalizes CC lowering.

**6. Code Emission**
Final cleanup and output to a elf object file.
Which then can be linked like normal.

## Status

> [!CAUTION]
> Currently targets **x86-64 only**. Some features are on purpose left out like LLVM style exception handling.
> And many features are still missing and correctness is not guaranteed.

## Differences to LLVM IR

I do not support some features like exception handling or the fp80 datatype. They will still work but some of their beheaviour will cause UB like throwing an exception.
I use basic block arguments instead of phi nodes and also do not support structs in my IR(instead instructions can return multiple values).

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
# Generate llvmir file from a c file use 01-03 with disable llvm optzns to enable frontend optimizations and not have functions marked as optnone
clang -O1 -mllvm -disable-llvm-optzns -fno-exceptions test.c -o test.ll -S -emit-llvm 
# Generate the object file via foptim 
./foptim_main ./test.ll ./test.o
#link the result
clang ./test.o ./test.out
```

> [!TIP]
> Theres also a foptim-cc which emulates clang to enable using foptim with cmake and friends. Also other scripts for specific usages in the scripts folder.

> [!TIP]
> The whole pipeline and many settings can be configured through a toml compiler configuration file or command line args
> look at default.toml or testconf.toml for some examples


## Tests
> [!IMPORTANT]
> Not included, cause they kinda thrown together and i dont know all the licenses/sources anymore.

The tests/benchmarks are all taken from public sources or written by myself.
Mostly taken from
+ GCCs github repo
+ LLVMs github repo
+ embench github repo
+ lz4/bzip2
+ tsvc
+ polybench
* cpython/perl/quickjs
+ fujitsu/compiler-test-suite github repo
+ CppBenchmark github repo
+ Some handwritten


## Bench
Some bench results and my perfomance overtime. The cases where I am faster might be caused by illegal beheaviour in my Compiler and not superior optimizations.

<img width="5006" height="3568" alt="bench_result" src="https://github.com/user-attachments/assets/0a381f19-a658-4f86-9cf7-5a73c76c77d8" />


## TODO
+ Compiler Stuff
	+ make flake.nix to actually be useful for building fully
	+ Parallelize Module Passes
		+ inlining
		+ IPCP
		+ argpromotion
		+ funcprop annotator
	+ Support more CCs
	+ add a custom frontend other then llvmir
	+ serialize
	+ deserialize
+ Optimization Stuff
	+ backwards constant prop (when transformation is applied to for example a buffer constant prop it backwards so its already applied to the constant values)
		+ sometimes already done in trivial cases
	+ replace cond + select with specific cmovXX commands
		+ missing for floatingpoint
	+ slpvec rn loads of f32x8 disabled since they might generate i64x8 inputs which we need to legalize then
	+ vectorize more
	+ loopoptim (got working prototype)
	+ better cost estimation
	+ More Passes
		+ Get SSAPRE
		+ Rework sccp

