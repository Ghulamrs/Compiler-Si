#!/usr/bin/env bash
#
# The fourth target's suite: every case compiled for the TMS320C6747, run on
# the VM6747 emulator beside the runtime cpp11 compiled for it, and compared
# with the output recorded from the app's interpreter - the same claim
# run.sh makes for the host, made for a target no machine here can execute.
#
#   SHC=... VM=... RUNTIME=... CC1=... ./tests/tms6747.sh [filter]
#
# RUNTIME is the directory of C6000 text `make tms6747` writes, one file per
# runtime source, which the emulator assembles whole. The compiler's own
# diagnostics come first in the recorded output, as in run.sh.
#
# Then one question the corpus cannot ask, since no runtime function takes
# that many: a call with more arguments than the ten registers carry, into
# a function another compiler wrote. c90 lays a C function's stack
# parameters out the way TI's cl6x was measured to - from the caller's
# B15 + 4, a word each, a double 8-aligned - so a foreign call that
# answers right says this compiler's callers do the same. Skipped, and
# said so, where there is no c90 to compile the C.

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

FILTER="${1:-}"
OUT=tests/out-tms6747
mkdir -p "$OUT"

SHC="${SHC:-./shalimar.exe}"
VM="${VM:-../Emulator/vm6747.exe}"
RUNTIME="${RUNTIME:-lib/shmrt-tms6747}"
[ -x "$SHC" ] || { echo "no $SHC - run make first, or set SHC=" >&2; exit 2; }
[ -x "$VM" ] || { echo "no emulator at $VM - build VM6747/Emulator, or set VM=" >&2; exit 2; }
[ -d "$RUNTIME" ] || { echo "no runtime at $RUNTIME - run 'make tms6747', or set RUNTIME=" >&2; exit 2; }

pass=0
fail=0

for case in tests/cases/*.shm tests/load/*.shm; do
    name=$(basename "$case" .shm)
    [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
    expected="${case%.shm}.expected"
    [ -f "$expected" ] || { echo "SKIP $name (nothing recorded)"; continue; }

    "$SHC" --target=tms6747 -S "$case" -o "$OUT/$name.s" > "$OUT/$name.compile" 2>/dev/null
    compiled=$?
    cp "$OUT/$name.compile" "$OUT/$name.actual"
    if [ $compiled -ne 0 ]; then
        if diff -u "$expected" "$OUT/$name.actual" > "$OUT/$name.diff" 2>&1; then
            pass=$((pass+1)); continue
        fi
        echo "FAIL $name (did not compile)"
        sed -n '1,12p' "$OUT/$name.diff"
        fail=$((fail+1)); continue
    fi

    "$VM" "$OUT/$name.s" "$RUNTIME" >> "$OUT/$name.actual" 2>&1 < /dev/null
    if diff -u "$expected" "$OUT/$name.actual" > "$OUT/$name.diff" 2>&1; then
        pass=$((pass+1))
    else
        echo "FAIL $name"
        sed -n '1,12p' "$OUT/$name.diff"
        fail=$((fail+1))
    fi
done

echo
echo "tms6747: $pass passed, $fail failed"

# ---- the foreign call past the registers ----------------------------------
CC1="${CC1:-../Compiler-Ci/c90.exe}"
if [ -n "$FILTER" ]; then
    :
elif [ ! -x "$CC1" ]; then
    echo "tms6747: the foreign call past ten arguments was not checked - no c90 at $CC1 (set CC1=)"
else
    # In a directory of its own: what stages out-tms6747 for TI's tools takes
    # every program there, and this pair is one program in two files.
    F="$OUT/foreign"; mkdir -p "$F"
    cat > "$F/foreign.c" <<'C'
int c_sum12(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l)
{ return a + b*2 + c*3 + d*4 + e*5 + f*6 + g*7 + h*8 + i*9 + j*10 + k*11 + l*12; }
double c_mix(int a, double x, int b, double y, int c, double z, int d, double w, int e, double v, int k, double l)
{ return a + x + b + y + c + z + d + w + e + v + k*100 + l*1000; }
double c_tail(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l, double m, int n)
{ return k + l*10 + m*100 + n*1000; }
C
    cat > "$F/foreign.shm" <<'S'
uses <int> = c_sum12(a: int, b: int, c: int, d: int, e2: int, f: int, g: int, h: int, i: int, j: int, k: int, l: int)
uses <real> = c_mix(a: int, x: real, b: int, y: real, c: int, z: real, d: int, w: real, e2: int, v: real, k: int, l: real)
uses <real> = c_tail(a: int, b: int, c: int, d: int, e2: int, f: int, g: int, h: int, i: int, j: int, k: int, l: int, m: real, n: int)
fun <> = main() {
  ? c_sum12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)
  ? c_mix(1, 0.5, 2, 0.25, 3, 0.125, 4, 0.0625, 5, 0.03125, 6, 0.5)
  ? c_tail(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 0.5, 3)
}
S
    printf '650 \n1115.9687500 \n3071.0000000 \n' > "$F/foreign.expected"
    if ! "$CC1" -S -arch tms6747 "$F/foreign.c" -o "$F/foreign-c.s" > "$F/foreign.cc" 2>&1; then
        echo "FAIL foreign: c90 refused the C"; sed -n '1,3p' "$F/foreign.cc"; fail=$((fail+1))
    elif ! "$SHC" --target=tms6747 -S "$F/foreign.shm" -o "$F/foreign.s" > "$F/foreign.compile" 2>&1; then
        echo "FAIL foreign: shalimar refused the program"; sed -n '1,3p' "$F/foreign.compile"; fail=$((fail+1))
    else
        "$VM" "$F/foreign.s" "$F/foreign-c.s" "$RUNTIME" > "$F/foreign.actual" 2>&1 < /dev/null
        if diff -u "$F/foreign.expected" "$F/foreign.actual" > "$F/foreign.diff" 2>&1; then
            echo "tms6747: the foreign call past ten arguments answers as c90's callee reads them"
        else
            echo "FAIL foreign: the arguments past the registers did not arrive"; sed -n '1,12p' "$F/foreign.diff"; fail=$((fail+1))
        fi
    fi
fi
[ $fail -eq 0 ]
