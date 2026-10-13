#!/usr/bin/env bash
#
# The debuggers, measured: each program in tests/m10 is built twice - the Shalimar one by this
# compiler with -g, the C one written beside it line for line by the machine's own C compiler
# with -g - and both are put through the host's debugger (lldb on a Mac, gdb on Linux) by
# tests/m10/m10.py, which compares where each stopped, what the stack held and what the
# variables read. The C program is the oracle: what clang's or gcc's debug information lets the
# debugger say, shalimar's must let it say too.
#
#   SHC=... ./tests/m10.sh [w1 w2 ...]

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

SHC="${SHC:-./shalimar.exe}"
[ -x "$SHC" ] || { echo "no $SHC - run make first, or set SHC=" >&2; exit 2; }
SHC="$(cd "$(dirname "$SHC")" && pwd)/$(basename "$SHC")"

case "$(uname -s)" in
    Darwin) DEBUGGER=lldb; CC="${CC:-clang}" ;;
    *)      DEBUGGER=gdb;  CC="${CC:-gcc}" ;;
esac
command -v "$DEBUGGER" >/dev/null || { echo "no $DEBUGGER on this machine" >&2; exit 2; }

OUT=tests/out-m10
mkdir -p "$OUT"
HERE="$(pwd)"
names="$*"
[ -n "$names" ] || names=$(cd tests/m10 && ls *.script | sed 's/\.script$//')

pass=0
fail=0
for w in $names; do
    src=tests/m10/$w.shl
    [ -f "$src" ] || src=tests/m10/$w.shm
    cp "$src" "$OUT/$w.shl"
    cp "tests/m10/$w.c" "$OUT/$w.c"
    # Built from inside the output directory, so the source names the debugger resolves are plain.
    (cd "$OUT" && "$SHC" -nologo -g --no-search "$w.shl" -o "$w" >/dev/null 2>&1 && "$CC" -g -O0 -o "${w}c" "$w.c") || {
        echo "FAIL $w (did not build)"; fail=$((fail+1)); continue; }
    if (cd "$OUT" && python3 "$HERE/tests/m10/m10.py" compare --debugger $DEBUGGER \
            "./$w" "$w.shl" "./${w}c" "$w.c" "$HERE/tests/m10/$w.script" > "$w.m10" 2>&1); then
        pass=$((pass+1)); tail -1 "$OUT/$w.m10"
    else
        echo "FAIL $w"; sed -n '1,40p' "$OUT/$w.m10"; fail=$((fail+1))
    fi
done

echo
echo "m10 ($DEBUGGER): $pass passed, $fail failed"
[ $fail -eq 0 ]
