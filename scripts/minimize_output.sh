#!/usr/bin/env bash 

build_dir="/home/tim/programming/foptim/build"
test_file="min.c"

foptim="$build_dir/foptim_main"

clang++ -O0 $test_file -o min.ll -S -emit-llvm || exit 1
clang++ -O1 $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o clang_min.out || exit 1
g++ -O1 $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o gcc_min.out || exit 1

$foptim min.ll min.o || exit 0
clang++ min.o -o min.out -lm || exit 1

echo "Running Mine"
./min.out
stats_got=$?
echo "Running Other"
./clang_min.out
stats_exp=$?
./gcc_min.out
stats_exp2=$?
echo "Exit MCG"
echo $stats_got
echo $stats_exp
echo $stats_exp2

if [[ $stats_got != $stats_exp ]]; then
if [[ $stats_exp != $stats_exp2 ]]; then
  echo "Bad Failed"
  exit 1
else
  echo "Good Failed"
  exit 0
fi
else
  exit 1
fi

