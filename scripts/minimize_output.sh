#!/usr/bin/env bash

# --- NEW: Flag Parsing ---
SHOW_TIMINGS=0
for arg in "$@"; do
    if [[ "$arg" == "--time" || "$arg" == "-t" ]]; then
        SHOW_TIMINGS=1
        break
    fi
done

# Variables to store elapsed times
time_clang=""
time_foptim=""

# --- NEW: Trap to always print at the very end ---
print_timings() {
    if [[ "$SHOW_TIMINGS" == "1" ]]; then
        echo "--- Timings ---"
        [[ -n "$time_clang" ]] && echo "clang took ${time_clang} sec"
        [[ -n "$time_foptim" ]] && echo "foptim took ${time_foptim} sec"
    fi
}
# This ensures it prints no matter which 'exit' command is triggered
trap print_timings EXIT

# --- NEW: Helper function to measure execution time ---
time_cmd() {
    local var_name=$1
    shift
    if [[ "$SHOW_TIMINGS" == "1" ]]; then
        local start=$(date +%s.%3N)
        "$@"
        local ret=$?
        local end=$(date +%s.%3N)
        # Calculate time diff and save it to the specified variable name
        local elapsed=$(awk "BEGIN {printf \"%.3f\", $end - $start}")
        eval "$var_name=\$elapsed"
        return $ret
    else
        # If no flag, just run the command normally
        "$@"
        return $?
    fi
}

# --- ORIGINAL SCRIPT STARTS HERE ---

TEST_FOLDER="$HOME/programming/foptim_test"
FOLDER="$HOME/programming/foptim"
BUILD_DIR="$FOLDER/build"
test_file="min.cpp"
foptim="$BUILD_DIR/foptim_main"
flags="-U__SIZEOF_INT128__ -std=c++26 -fno-stack-protector"
test_linkdir="-I$TEST_FOLDER/test/CppPerformanceBenchmarks/ -I$TEST_FOLDER/test/embench/"
compile_optim="-O1 -mllvm -disable-llvm-optzns"

UNINTERESTING=1  # cvise discards these
INTERESTING=0    # cvise keeps these

clang++ $compile_optim $flags $test_linkdir "$test_file" -o min.ll -S -emit-llvm \
  || exit $UNINTERESTING

clang++ -static-libstdc++ -O3 $flags $test_linkdir "$test_file" \
  -Werror=return-type -Werror=uninitialized -Wall -Wextra \
  -o clang_min.out 2>/dev/null || exit $UNINTERESTING

g++ -static-libstdc++ -O0 $flags $test_linkdir "$test_file" \
  -Werror=return-type -Werror=uninitialized -Wall -Wextra \
  -o gcc_min.out 2>/dev/null || exit $UNINTERESTING


echo "CompileClang"
time_cmd time_clang clang++ -O3 $flags min.ll -c -o /dev/null 2>/dev/null 

# clang++ -static-libstdc++ -O3 $flags $test_linkdir "$test_file" \
#   -Werror=return-type -Werror=uninitialized -Wall -Wextra \
#   -emit-llvm -S -o -


echo "COMPILE"
time_cmd time_foptim timeout 20s $foptim --cconffile "$FOLDER/src/testconf.toml" min.ll min.o \
  || exit $INTERESTING

echo "LINK"
clang++ min.o -o min.out -static-libstdc++ \
  || exit $UNINTERESTING


echo "M"
echo "C"
echo "G"
OUT_exp=$(./clang_min.out 2>&1);  stats_exp=$?
OUT_exp2=$(./gcc_min.out 2>&1);   stats_exp2=$?
OUT_got=$(./min.out 2>&1); stats_got=$?

echo $OUT_got
echo $stats_got
echo $OUT_exp
echo $stats_exp
echo $OUT_exp2
echo $stats_exp2

if [[ "$stats_exp" != "$stats_exp2" ]] || [[ "$OUT_exp" != "$OUT_exp2" ]]; then
  echo "Fail1"
  exit $UNINTERESTING
fi

# if [[ "$stats_exp" != "0" ]] || [[ "$OUT_exp" != "33.000000" ]]; then
if [[ "$stats_exp" != "0" ]]; then
  echo "Fail2"
  exit $UNINTERESTING
fi
  
if [[ "$stats_got" != "$stats_exp" ]] || [[ "$OUT_got" != "$OUT_exp" ]]; then
  echo "Good"
  exit $INTERESTING
fi

echo "Boring"
exit $UNINTERESTING
