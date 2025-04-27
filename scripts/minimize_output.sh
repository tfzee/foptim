#!/usr/bin/env bash 

build_dir="/home/tim/programming/foptim/build"
test_file="min.c"

foptim="$build_dir/foptim_main"
flags="-fno-exceptions -fno-stack-protector"
test_linkdir="-I/home/tim/programming/foptim/test/llvm_benchmark_adobe_cpp/"

clang++ -O3 -mllvm -disable-llvm-optzns $flags $test_linkdir $test_file -o min.ll -S -emit-llvm || exit 1
clang++ -O2 $flags $test_linkdir $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o clang_min.out || exit 1
g++ -O2 $flags $test_linkdir $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o gcc_min.out || exit 1

$foptim min.ll min.o || exit 0
clang++ min.o -o min.out -lm || exit 1

echo "Running Mine"
OUT_got=$(./min.out 2>&1)
stats_got=$?
echo "Running clang"
OUT_exp=$(./clang_min.out 2>&1)
stats_exp=$?
echo "Running gcc"
OUT_exp2=$(./gcc_min.out 2>&1)
stats_exp2=$?
echo "Exit MCG"
echo "$OUT_got"
echo $stats_got
echo "$OUT_exp"
echo $stats_exp
echo "$OUT_exp2"
echo $stats_exp2

if [[ "$stats_got" != "$stats_exp" ]] || [[ "$OUT_got" != "$OUT_exp" ]]; then
if [[ "$stats_exp" != "$stats_exp2" ]] || [[ "$OUT_exp" != "$OUT_exp2" ]]; then
# if [[ "$stats_got" != "$stats_exp" ]]; then
# if [[ "$stats_exp" != "$stats_exp2" ]]; then
  echo "Bad Failed"
  exit 1
else
  echo "Good Failed"
  exit 0
fi
else
  exit 1
fi

