#!/usr/bin/env bash
set -xe

CLANG=clang
CLANGXX=clang++
LLVMLINK=llvm-link
OPTIMIZER=~/programming/foptim/build/foptim_main

TMPDIR=$(mktemp -d)
cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT

OUT=""
COMPILE_ONLY=0
ASSEMBLE_ONLY=0
PREPROCESS_ONLY=0
LANG_CXX=0

EXTRA_CLANG_FLAGS=()
EXTRA_LD_FLAGS=()
BC_INPUTS=()
OBJ_INPUTS=()
SRC_INPUTS=()

while [ $# -gt 0 ]; do
    case "$1" in
        # output file
        -o)
            OUT="$2"; shift 2;;
        # cmake autoconf preprocess checks
        -E)
            PREPROCESS_ONLY=1;EXTRA_CLANG_FLAGS+=("$1");  shift;;
        # compatibility with configure queries
        -dumpmachine|-dumpversion|-print-search-dirs)
            "$CLANG" "$1"
            exit 0;;
        -v)
            clang++ -v
            echo "FOPTIM:"
            $OPTIMIZER -v
            exit 0;;
        -c)
            COMPILE_ONLY=1;EXTRA_CLANG_FLAGS+=("$1");  shift;;
        -S)
            ASSEMBLE_ONLY=1;EXTRA_CLANG_FLAGS+=("$1");  shift;;
        -x)
            [ "$2" = "c++" ] && LANG_CXX=1
            shift 2;;
        -I*|-D*|-O*|-std=*|-f*|-m*|-W*)
            EXTRA_CLANG_FLAGS+=("$1"); shift;;
        -MF|-MT|-MQ)
            EXTRA_CLANG_FLAGS+=("$1" "$2"); shift 2;;
        -MD|-MMD)
            EXTRA_CLANG_FLAGS+=("$1"); shift;;
        -L*|-l*)
            EXTRA_LD_FLAGS+=("$1"); shift;;
        *.o)
            OBJ_INPUTS+=("$1"); shift;;
        *.bc)
            BC_INPUTS+=("$1"); shift;;
        *)
            if [ "$1" = "-" ]; then
                # store it into temp file to read that
                SRC="$TMPDIR/stdin_input.c"
                cat > "$SRC"
                SRC_INPUTS+=("$SRC")
                shift
            elif [[ "$1" =~ \.(c|cc|cpp|cxx)$ ]]; then
                SRC_INPUTS+=("$1")
                shift
            else
                EXTRA_CLANG_FLAGS+=("$1")
                EXTRA_LD_FLAGS+=("$1")
                shift
            fi
            ;;
        esac
done

# --- might get empty shit catch it here -------------------------------------------------------------
if [ $COMPILE_ONLY -eq 1 ] &&
   [ ${#SRC_INPUTS[@]} -eq 0 ] &&
   [ ${#BC_INPUTS[@]} -eq 0 ] &&
   [ ${#OBJ_INPUTS[@]} -eq 0 ]; then

    [ -z "$OUT" ] && OUT="out.o"
    echo "/* empty */" | $CC -x c -c -o "$OUT" -
    exit 0
fi
if [ $PREPROCESS_ONLY -eq 0 ] &&
   [ ${#SRC_INPUTS[@]} -eq 0 ] &&
   [ ${#OBJ_INPUTS[@]} -eq 0 ] &&
   [ ${#BC_INPUTS[@]} -eq 0 ]; then
    echo "driver.sh: no input files" >&2
    exit 1
fi

# --- find which lang -------------------------------------------------
if [ $LANG_CXX -eq 1 ] ||
   { [ ${#SRC_INPUTS[@]} -gt 0 ] && [[ ${SRC_INPUTS[0]} =~ \.(cpp|cc|cxx)$ ]]; }; then
    CC=$CLANGXX
else
    CC=$CLANG
fi

# --- prerpocesse only ----------------------------------------
if [ $PREPROCESS_ONLY -eq 1 ]; then
    if [ ${#SRC_INPUTS[@]} -eq 0 ]; then
        SRC="$TMPDIR/empty.c"
        echo '/* empty */' > "$SRC"
        SRC_INPUTS+=("$SRC")
        [ -z "$OUT" ] && OUT="$TMPDIR/empty.i"
    fi

    "$CC" -E "${EXTRA_CLANG_FLAGS[@]}" "${SRC_INPUTS[@]}" -o "$OUT"
    exit $?
fi

# --- to bitcode --------------------------------------
for SRC in "${SRC_INPUTS[@]}"; do
    BASE=$(basename "$SRC")
    STEM=${BASE%.*}
    BC="$TMPDIR/$STEM.bc"

    "$CC" -S -emit-llvm -fno-exceptions -mllvm -disable-llvm-optzns \
        "${EXTRA_CLANG_FLAGS[@]}" "$SRC" -o "$BC"

    if [ $ASSEMBLE_ONLY -eq 1 ]; then
        [ -z "$OUT" ] && OUT="${STEM}.bc"
        cp "$BC" "$OUT"
        continue
    fi

    BC_INPUTS+=("$BC")
done

if [ $ASSEMBLE_ONLY -eq 1 ]; then
    exit 0
fi

# --- link it first so we can parallelize + lot ----------------------------------------------
FINAL_OBJ=""
if [ ${#BC_INPUTS[@]} -gt 0 ]; then
    COMBINED_BC="$TMPDIR/combined.bc"

    if [ ${#BC_INPUTS[@]} -eq 1 ]; then
        cp "${BC_INPUTS[0]}" "$COMBINED_BC"
    else
        $LLVMLINK "${BC_INPUTS[@]}" -o "$COMBINED_BC"
    fi

    # --- run foptim ----------------------------------------
    FINAL_OBJ="$TMPDIR/foptim.o"
    "$OPTIMIZER" "$COMBINED_BC" "$FINAL_OBJ"

    if [ $COMPILE_ONLY -eq 1 ]; then
        [ -z "$OUT" ] && OUT="out.o"
        cp "$FINAL_OBJ" "$OUT"
        exit 0
    fi
fi

# --- link to external and convert into binary ------------------------------------------------------
[ -z "$OUT" ] && OUT="a.out"

ALL_OBJECTS=()
[ -n "$FINAL_OBJ" ] && ALL_OBJECTS+=("$FINAL_OBJ")
ALL_OBJECTS+=("${OBJ_INPUTS[@]}")

if [ ${#ALL_OBJECTS[@]} -eq 0 ]; then
    echo "driver.sh: nothing to link" >&2
    exit 1
fi

"$CC" "${ALL_OBJECTS[@]}" "${EXTRA_LD_FLAGS[@]}" -o "$OUT"
