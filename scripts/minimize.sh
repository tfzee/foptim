#!/usr/bin/env bash 

TEST_FOLDER="$HOME/programming/foptim_test"
build_dir="$HOME/programming/foptim/build"
test_file="min.cpp"

failure_string="Cant have a isntruction referencing itself"

flags="-U__SIZEOF_INT128__ -std=c++26 -fno-stack-protector"
test_linkdir="-I$TEST_FOLDER/test/CppPerformanceBenchmarks/ -I$TEST_FOLDER/test/embench/"

foptim="$build_dir/foptim_main"

clang $flags $test_linkdir -O0 $test_file -o min.ll -S -emit-llvm || exit 1

compile_output=$($foptim min.ll min.o 2>&1)
echo "$compile_output"
if [[ $compile_output == *"$failure_string"* ]]; then
  exit 0
else
  exit 1
fi
