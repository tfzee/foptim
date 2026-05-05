#!/usr/bin/env bash
TEST_FOLDER="$HOME/programming/foptim_test"
FOLDER="$HOME/programming/foptim"
BUILD_DIR="$FOLDER/build"
test_file="min.cpp"
foptim="$BUILD_DIR/foptim_main"
flags="-U__SIZEOF_INT128__ -std=c++26 -fno-stack-protector"
test_linkdir="-I$TEST_FOLDER/test/CppPerformanceBenchmarks/ -I$TEST_FOLDER/test/embench/"
compile_optim="-O0"

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



$foptim --cconffile "$FOLDER/src/testconf.toml" min.ll min.o 2>/dev/null \
  || exit $INTERESTING

clang++ min.o -o min.out -static-libstdc++ 2>/dev/null \
  || exit $UNINTERESTING



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

if [[ "$stats_exp" != "0" ]] || [[ "$OUT_exp" != "1.00000026.000000" ]]; then
  echo "Fail2"
  exit $UNINTERESTING
fi
  
if [[ "$stats_got" != "$stats_exp" ]] || [[ "$OUT_got" != "$OUT_exp" ]]; then
  echo "Good"
  exit $INTERESTING
fi

echo "Boring"
exit $UNINTERESTING
