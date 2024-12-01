#!/usr/bin/env bash 

build_dir="/home/tim/programming/foptim/build"
test_file="/home/tim/programming/foptim/test/min.c"

failure_string="Instr has invalid number of basicblock arguments"


foptim="$build_dir/foptim_main"

clang -O0 "$test_file" -o "min.ll" -S -emit-llvm || exit 1

compile_output=$($foptim "min.ll" "min.ss" 2>&1)
echo "$compile_output"
if [[ $compile_output == *"$failure_string"* ]]; then
  exit 0
else
  exit 1
fi
