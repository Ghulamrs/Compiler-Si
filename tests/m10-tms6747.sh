#!/usr/bin/env bash
#
# The C6747's debuggers, measured (D5): each program in tests/m10 built by this compiler with -g for
# tms6747 and by cpp11 -g from the C program beside it (as C++, printf declared extern "C" - cpp11
# compiles no C), the two put through `vm6747 --debug` on the assembly and, linked by asm6x and
# lnk6x against TI's runtime, through `sim6747 --run --debug` on the image. Lines, steps and the
# stack: the C6747 path carries no types or locations, and the scripts ask for none.
#
#   SHC=... CPP11=... ./tests/m10-tms6747.sh [w1 ...]
#
# The tools are found beside the tree - ../C++Optimize/cpp11.exe, ../../TI/{VM6747,SIM6747,ASM6x,
# LNK6x} - or named: CPP11, VM6747, SIM6747, ASM6X, LNK6X, TILIB (a directory holding
# rts6740_elf_eh.lib). The runtime is lib/shmrt-tms6747, as `make tms6747` writes it.

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
HERE="$(pwd)"

SHC="${SHC:-./shalimar.exe}"
CPP11="${CPP11:-../C++Optimize/cpp11.exe}"
VM6747="${VM6747:-../../TI/VM6747/vm6747.exe}"
SIM6747="${SIM6747:-../../TI/SIM6747/sim6747.exe}"
ASM6X="${ASM6X:-../../TI/ASM6x/build/asm6x.exe}"
LNK6X="${LNK6X:-../../TI/LNK6x/build/lnk6x.exe}"
TILIB="${TILIB:-../../TI/LNK6x/tests/ref}"
RUNTIME="${RUNTIME:-lib/shmrt-tms6747}"
for t in "$SHC" "$CPP11" "$VM6747" "$SIM6747" "$ASM6X" "$LNK6X"; do
    [ -x "$t" ] || { echo "no $t - build it, or name it" >&2; exit 2; }
done
[ -d "$RUNTIME" ] || { echo "no runtime at $RUNTIME - run 'make tms6747'" >&2; exit 2; }
abs() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
SHC=$(abs "$SHC"); CPP11=$(abs "$CPP11"); ASM6X=$(abs "$ASM6X"); LNK6X=$(abs "$LNK6X")
TILIB=$(cd "$TILIB" && pwd); RUNTIME=$(cd "$RUNTIME" && pwd)
export M10_VM6747="$(abs "$VM6747")" M10_SIM6747="$(abs "$SIM6747")"

OUT=tests/out-m10-tms6747
mkdir -p "$OUT"
names="$*"
[ -n "$names" ] || names=$(cd tests/m10 && ls *.c6x.script | sed 's/\.c6x\.script$//')

pass=0
fail=0
for w in $names; do
    cp "tests/m10/$w.shl" "$OUT/$w.shl"
    # cpp11's counterpart: the same C, line for line, with printf given C linkage.
    sed 's/^int printf(const char \*, \.\.\.);/extern "C" int printf(const char *, ...);/' "tests/m10/$w.c" > "$OUT/$w.cpp"
    script="$HERE/tests/m10/$w.c6x.script"
    ( cd "$OUT" &&
      "$SHC" -nologo -g --no-search --target=tms6747 -S "$w.shl" -o "$w.s" >/dev/null 2>&1 &&
      "$CPP11" -nologo -arch tms6747 -g -S "$w.cpp" -o "${w}c.s" >/dev/null 2>&1 &&
      SHALIMAR_AS="$ASM6X" SHALIMAR_LD="$LNK6X" SHALIMAR_TILIB="$TILIB" \
          "$SHC" -nologo -g --no-search --target=tms6747 --runtime="$RUNTIME" "$w.shl" -o "$w.out" >/dev/null 2>&1 &&
      CPP11_AS="$ASM6X" CPP11_LD="$LNK6X" CPP11_TILIB="$TILIB" \
          "$CPP11" -nologo -arch tms6747 -g "$w.cpp" -o "${w}c.out" >/dev/null 2>&1 ) || {
        echo "FAIL $w (did not build)"; fail=$((fail+1)); continue; }
    for dbg in vm6747 sim6747; do
        if [ $dbg = vm6747 ]; then ours="$w.s $RUNTIME"; theirs="${w}c.s"; else ours="$w.out"; theirs="${w}c.out"; fi
        if (cd "$OUT" && python3 "$HERE/tests/m10/m10.py" compare --debugger $dbg \
                "$ours" "$w.shl" "$theirs" "$w.cpp" "$script" > "$w.$dbg.m10" 2>&1); then
            pass=$((pass+1)); tail -1 "$OUT/$w.$dbg.m10"
        else
            echo "FAIL $w under $dbg"; sed -n '1,40p' "$OUT/$w.$dbg.m10"; fail=$((fail+1))
        fi
    done
done

echo
echo "m10 (tms6747): $pass passed, $fail failed"
[ $fail -eq 0 ]
