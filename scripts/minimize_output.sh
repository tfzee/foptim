#!/usr/bin/env bash 


TEST_FOLDER="$HOME/programming/foptim_test"
FOLDER="$HOME/programming/foptim"
BUILD_DIR="$FOLDER/build"

test_file="min.cpp"
foptim="$BUILD_DIR/foptim_main"
# flags="-U__SIZEOF_INT128__ -std=c++26 -fno-exceptions -fno-stack-protector"
flags="-U__SIZEOF_INT128__ -std=c++26 -fno-stack-protector"
# flags=""
test_linkdir="-I$TEST_FOLDER/test/CppPerformanceBenchmarks/ -I$TEST_FOLDER/test/embench/"
# compile_optim="-O3 -mllvm -disable-llvm-optzns"
compile_optim="-O0"

clang++ $compile_optim $flags $test_linkdir $test_file -o min.ll -S -emit-llvm || exit 1
clang++ -static-libstdc++ -O3 $flags $test_linkdir $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o clang_min.out || exit 1
g++ -static-libstdc++ -O0 $flags $test_linkdir $test_file -Werror=return-type -Werror=uninitialized -fsanitize=undefined -Wall -Wextra -o gcc_min.out || exit 1

$foptim min.ll min.o || exit 0
clang++ min.o -o min.out -static-libstdc++ || exit 1

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

# if [[ "$stats_exp" != "$stats_exp2" ]] || [[ "$OUT_exp" != "$OUT_exp2" ]] || [[ "$OUT_exp" != *"2450.000000"* ]] || [[ "$stats_exp" != "0" ]]; then
# if [[ "$stats_exp" != "$stats_exp2" ]] || [[ "$OUT_exp" != "$OUT_exp2" ]] || [[ "$stats_exp" != "0" ]]; then
if [[ "$stats_got" != "$stats_exp" ]] || [[ "$OUT_got" != "$OUT_exp" ]]; then
if [[ "$stats_exp" != "$stats_exp2" ]] || [[ "$OUT_exp" != "$OUT_exp2" ]] || [[ "$stats_exp" != "0" ]]; then
  echo "Bad Failed"
  exit 1
else
  echo "Good Failed"
  exit 0
fi
else
  exit 1
fi

