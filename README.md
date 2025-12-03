# Foptim: A relatively simple optimizing backend

Currently taking in llvmir converting it to internal representation(FIR).
Then it gets optimized by different passes.
A tree matching is then applied to the FIR for instruction selection generating MIR.
MIR then is further optimized + legalized.
It then does the first stage of transforming calls to adhere to the Calling Convention then does
basic register allocation and then finalizes the CC stuff.
Finally doing some cleanup and then outputing it into a Object file.
Currently and probably only ever supporting
X86-64 and theres still many missing features and I dont give any guarantees on correctness.

The tests are taken from
+ GCCs github repo
+ LLVMs github repo
+ embench github repo
+ fujitsu/compiler-test-suite github repo
+ Some handwritten

## Build instructions
Either use the flake devshell or need to install minimal dependencies manually
+ elfio
+ fmt
+ ninja
+ argparse
+ clang + llvm
+ gtest
+ ... prob missing something
For running all the tests
We also need lit and filecheck

```
mkdir build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++
ninja
# Generate llvmir file from a c file
clang -O1 -mllvm -disable-llvm-optzns -fno-exceptions -fno-stack-protector test.c -o test.ll -S -emit-llvm 
# Generate the object file via foptim 
./foptim_main ./test.ll ./test.o   
clang ./test.o ./test.out   
# To run all the tests(should all pass other then embench)
ninja check
```

## TODO
+ make flake.nix to actually be useful(or like it should be)
+ replace std::hash with the ankerl version + replace std::map/set with ankerl
+ replace cond + select with specific cmovXX commands
	+ missing for floatingpoint
+ no register spilling rn
+ optimize llvm intrin lowering like memset/memcopy
	+ partially done
+ 2 allocas without overlapping lifetimes could be merged to use same stack space
+ optimize sdiv
	+ can convert into imul shifts and add (theres some formular for this)
+ add a custom frontend other then llvmir
+ serialize
+ deserialize
+ Abstract away graph based matcher (maybe a DSL)
	+ Could be used in general like LLVMs matcher stuff in optimization passes
+ More Passes
	+ Get SSAPRE
	+ Rework sccp

