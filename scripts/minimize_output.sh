#!/usr/bin/env bash 

build_dir="/home/tim/programming/foptim/build"
test_file="min.c"

foptim="$build_dir/foptim_main"

clang++ -O0 $test_file -o min.ll -S -emit-llvm || exit 1
clang++ -O1 $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o clang_min.out || exit 1

$foptim min.ll min.o || exit 0
clang min.o -o min.out || exit 1

./min.out
stats_got=$?
./clang_min.out
stats_exp=$?
echo $stats_got
echo $stats_exp

if [[ $stats_got != $stats_exp ]] && [[ $stats_exp != 132 ]]; then
  echo "Good Failed"
  exit 0
else
  exit 1
fi

