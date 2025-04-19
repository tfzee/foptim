#!/usr/bin/env bash 

build_dir="/home/tim/programming/foptim/build"
test_file="min.c"

foptim="$build_dir/foptim_main"
flags="-fno-exceptions -fno-stack-protector -march=native"
test_linkdir="-I/home/tim/programming/foptim/test/llvm_benchmark_adobe_cpp/"

clang++ -O3 -mllvm -disable-llvm-optzns $flags $test_linkdir $test_file -o min.ll -S -emit-llvm
clang++ -O3 $flags $test_linkdir $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o clang_min.out
g++ -O3 $flags $test_linkdir $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o gcc_min.out

$foptim min.ll min.o || exit 0
clang++ min.o -o min.out -lm || exit 1

echo "Running"
hyperfine "./min.out" "./clang_min.out" "./gcc_min.out"

