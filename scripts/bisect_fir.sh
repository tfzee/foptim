#!/usr/bin/env bash
# Usage: scripts/bisect_fir.sh <file.ll> [extra foptim_main args...]
# Finds the first FIR pass index N (--bisect N) whose --print-fir output differs
# between two runs, i.e. the first pass that makes the output nondeterministic.
# Run from anywhere, uses build/foptim_main and src/testconf.toml.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/foptim_main"
CONF="${CONF:-$ROOT/src/testconf.toml}"
IN="$1"; shift
TMP="$(mktemp -d)"
run() { "$BIN" --cconffile "$CONF" --workers 0 "$IN" --print-fir --bisect "$1" "${@:2}" 2>/dev/null | md5sum; }
differs() { [ "$(run "$1" "${@:2}")" != "$(run "$1" "${@:2}")" ]; }

N=$("$BIN" --cconffile "$CONF" --workers 0 "$IN" --print-fir --bisect 0 2>&1 >/dev/null \
    | sed -n 's/^Having \([0-9]*\) FIR passes.*/\1/p')
lo=0; hi=$((N - 1))
if ! differs "$hi" "$@"; then echo "no difference at full depth ($hi)"; exit 0; fi
while [ "$lo" -lt "$hi" ]; do
  mid=$(((lo + hi) / 2))
  if differs "$mid" "$@"; then hi=$mid; else lo=$((mid + 1)); fi
done
echo "first nondeterministic pass index: $lo"
"$BIN" --cconffile "$CONF" --workers 0 "$IN" --print-fir --bisect "$lo" 2>&1 >/dev/null | grep "^X $lo:"
rm -rf "$TMP"
