#!/usr/bin/env bash 

build_dir="/home/tim/programming/foptim/build"
test_file="min.cpp"

foptim="$build_dir/foptim_main"
flags="-fno-exceptions -fno-stack-protector"
test_linkdir="-I/home/tim/programming/foptim/test/llvm_benchmark_adobe_cpp/"
compile_optim="-O3 -mllvm -disable-llvm-optzns"
# compile_optim="-O0"

clang++ $compile_optim $flags $test_linkdir $test_file -o min.ll -S -emit-llvm || exit 1
clang++ -static-libstdc++ -O2 $flags $test_linkdir $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o clang_min.out || exit 1
g++ -static-libstdc++ -O2 $flags $test_linkdir $test_file -Werror=return-type -Werror=uninitialized -Wall -Wextra -o gcc_min.out || exit 1

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

if [[ "$stats_exp" == "$stats_exp2" ]] || [[ "$OUT_exp" == "$OUT_exp2" ]] ||  [[ "$stats_got" == "$stats_exp" ]] || [[ "$OUT_got" == "$OUT_exp" ]]; then
perf=$(hyperfine -i -N --style=basic -n mine ./min.out -n clang ./clang_min.out -n gcc ./gcc_min.out | tail -1 | xargs | cut -d' ' -f1)
echo "$perf Times" 
within_range=$(awk -v p="$perf" 'BEGIN { if ((p - 5) > 0) print "true"; else print "false" }')
if [[ "$within_range" == "true" ]]; then
  echo "Good Failed"
  exit 0
else
  echo "Bad Failed2"
  exit 1
fi
else
  echo "Bad Failed1"
  exit 1
fi

