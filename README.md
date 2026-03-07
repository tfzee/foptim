# Foptim: A relatively simple optimizing backend

Currently taking in llvmir converting it to internal representation(FIR).
Then it gets optimized by different function/module passes. While function passes run in parrallel not all module passes are parallelized yet.
A tree matching is then applied to the FIR for instruction selection generating MIR.
MIR then is further optimized + legalized.
It then does the first stage of transforming calls to adhere to the Calling Convention then does
basic register allocation and then finalizes the CC stuff.
Finally doing some cleanup and then outputing it into a Object file.

> [!CAUTION]
> Currently and probably only ever supporting X86-64 and theres still many missing features.
> I dont give any guarantees on correctness.

## Build instructions
Either use the flake devshell or need to install minimal dependencies manually

| Dependency | For What |
| --- | --- |
| elfio | for outputting the elf obj file |
| fmt | for printing | 
| argparse | for parsing command line arguments | 
| gtest | for a few unit tests | 
| clang+llvm | optional if using the foptim_main to load in llvmir | 
| ninja | optional for building| 
| ... maybe missing some | .... | 

I use these commands for building
```
mkdir build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++
ninja
# Generate llvmir file from a c file
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
> Not included, cause they kinda thrown together and i dont know all the licenses.

The tests are taken from
+ GCCs github repo
+ LLVMs github repo
+ embench github repo
+ fujitsu/compiler-test-suite github repo
+ Some handwritten

## Bench
Some bench results and perfomance overtime the alst regression in perfomance was from the removal of the current SLP pass to add a newer one. And maybe some other fixes causing slowdowns.

<img width="5006" height="3568" alt="bench_result" src="https://github.com/user-attachments/assets/0a381f19-a658-4f86-9cf7-5a73c76c77d8" />


## TODO
+ make flake.nix to actually be useful(or like it should be)
+ replace std::hash with the ankerl version + replace std::map/set with ankerl
+ replace cond + select with specific cmovXX commands
	+ missing for floatingpoint
+ no register spilling rn
+ 2 allocas without overlapping lifetimes could be merged to use same stack space
+ optimize sdiv
	+ can convert into imul shifts and add (theres some formular for this)
+ add a custom frontend other then llvmir
+ serialize
+ deserialize
+ Abstract away graph based matcher (maybe a DSL)
	+ Could be used in general like LLVMs matcher stuff in optimization passes
+ Parallelize Passes
+ Support more CCs
+ More Passes
	+ Get SSAPRE
	+ Rework sccp

