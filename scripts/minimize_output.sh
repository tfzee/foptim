#!/usr/bin/env bash 

build_dir="/home/tim/programming/foptim/build"
test_file="min.c"

foptim="$build_dir/foptim_main"

clang -O0 $test_file -o min.ll -S -emit-llvm || exit 1
$foptim min.ll min.ss || exit 1
nasm min.ss -felf64 -g -F dwarf || exit 1
ld min.o -o min.out || exit 1

./min.out
stats=$?
echo $stats
if [[ $stats != 0 ]]; then
  exit 0
else
  exit 1
fi

