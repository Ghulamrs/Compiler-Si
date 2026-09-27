#!/usr/bin/env bash
#
# The host suite: compile every case for this machine, run it, and compare
# with the output recorded beside it.
#
# The recorded output comes from the app's own interpreter (tests/record.sh),
# so a case that passes here says the compiler and the interpreter agree -
# which is the only claim worth making about a second implementation of a
# language that already has one.
#
# Two directories: tests/cases asks whether a rule of the language is obeyed,
# tests/load asks whether it is still obeyed when the program is large. The
# second found two defects the first could not reach - see tests/generate.py.
#
#   ./tests/run.sh              every case
#   ./tests/run.sh gcd          cases whose name contains "gcd"

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

FILTER="${1:-}"
OUT=tests/out
mkdir -p "$OUT"

# Overridable, because a workspace build puts every binary in one directory
# rather than in each repository's own root - and after a clean there is no
# ./shc.exe here at all.
SHC="${SHC:-./shc.exe}"
[ -x "$SHC" ] || { echo "no $SHC - run make first, or set SHC=" >&2; exit 2; }

# One case, its report and verdict beside its output, so that the cases run JOBS at a time.
one() {
    case=$1
    name=$(basename "$case" .shm)
    expected="${case%.shm}.expected"
    if [ ! -f "$expected" ]; then
        echo "SKIP $name (nothing recorded)"
        return
    fi

    "$SHC" "$case" -o "$OUT/$name" > "$OUT/$name.compile" 2>/dev/null
    compiled=$?
    cp "$OUT/$name.compile" "$OUT/$name.actual"
    if [ $compiled -ne 0 ]; then
        if diff -u "$expected" "$OUT/$name.actual" > "$OUT/$name.diff" 2>&1; then
            echo pass > "$OUT/$name.verdict"; return
        fi
        echo "FAIL $name (did not compile)"
        sed -n '1,12p' "$OUT/$name.diff"
        echo fail > "$OUT/$name.verdict"; return
    fi

    "$OUT/$name" >> "$OUT/$name.actual" 2>&1 < /dev/null
    if diff -u "$expected" "$OUT/$name.actual" > "$OUT/$name.diff" 2>&1; then
        echo pass > "$OUT/$name.verdict"
    else
        echo "FAIL $name"
        sed -n '1,12p' "$OUT/$name.diff"
        echo fail > "$OUT/$name.verdict"
    fi
}
if [ "$FILTER" = --one ]; then one "$2" > "$OUT/$(basename "$2" .shm).report" 2>&1; exit 0; fi

cases() {
    for case in tests/cases/*.shm tests/load/*.shm; do
        name=$(basename "$case" .shm)
        [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
        echo "$case"
    done
}
rm -f "$OUT"/*.verdict "$OUT"/*.report
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
cases | xargs -P "$JOBS" -n 1 bash tests/run.sh --one
for case in $(cases); do cat "$OUT/$(basename "$case" .shm).report"; done
pass=$(cat "$OUT"/*.verdict 2>/dev/null | grep -cx pass || true)
fail=$(cat "$OUT"/*.verdict 2>/dev/null | grep -cx fail || true)

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
