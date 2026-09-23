#!/bin/sh
# The tms6747 target through asm6x, the project's own C6000 assembler: every
# case tests/tms6747.sh compiles is taken to a TI object by the driver itself
# (`shalimar --target=tms6747 -c`, which runs asm6x on what it wrote), and that
# object must be the one asm6x writes for the same assembly by hand - every
# table of it, the file symbol aside, which names the temporary. A case the
# compiler refuses is the corpus's business (tests/run.sh, tests/tms6747.sh)
# and is passed over here. That asm6x's objects are TI's is ASM6x's own suite;
# that TI links them, with the runtime's, is tests/windows/ti-link.cmd on the
# box, where lnk6x is (tests/ti-link.sh runs it).
#
#   ASM6X=<asm6x> sh tests/asm6x.sh        the assembler: $ASM6X, else ASM6x's
#                                          build beside this tree, else one on PATH
set -u
cd "$(dirname "$0")/.."
SHC="${SHC:-./shalimar.exe}"
ASM6X="${ASM6X:-}"
if [ -z "$ASM6X" ]; then
    for c in ../../ASM6x/build/asm6x.exe "$HOME/asm6x-build/asm6x.exe" "$(command -v asm6x 2>/dev/null)"; do
        [ -n "$c" ] && [ -x "$c" ] && { ASM6X=$c; break; }
    done
fi
[ -n "$ASM6X" ] || { echo "asm6x.sh: no asm6x - name it with ASM6X=<path>"; exit 2; }
C6XDIFF="${C6XDIFF:-../../ASM6x/tests/c6xdiff.py}"
[ -f "$C6XDIFF" ] || C6XDIFF="$HOME/asm6x-build/tests/c6xdiff.py"
[ -f "$C6XDIFF" ] || { echo "asm6x.sh: no c6xdiff.py - name it with C6XDIFF=<path>"; exit 2; }
OUT=tests/out-asm6x
rm -rf "$OUT"; mkdir -p "$OUT"

pass=0; fail=0; refused=0
for case in tests/cases/*.shm tests/load/*.shm; do
    [ -f "$case" ] || continue
    name=$(basename "$case" .shm)
    case "$name" in *" "*) continue;; esac
    if ! "$SHC" --target=tms6747 -nologo -S "$case" -o "$OUT/$name.s" > /dev/null 2>&1; then
        refused=$((refused + 1)); continue
    fi
    if ! ( ulimit -t 20; SHALIMAR_AS="$ASM6X" "$SHC" --target=tms6747 -nologo -c "$case" -o "$OUT/$name.obj" < /dev/null ) > "$OUT/$name.err" 2>&1; then
        echo "FAIL $name: the driver did not make an object"
        sed 's/^/      /' "$OUT/$name.err" | head -3
        fail=$((fail + 1)); continue
    fi
    if ! "$ASM6X" "$OUT/$name.s" -o "$OUT/$name.hand.obj" > "$OUT/$name.hand.err" 2>&1; then
        echo "FAIL $name: asm6x refused the assembly by hand"
        sed 's/^/      /' "$OUT/$name.hand.err" | head -3
        fail=$((fail + 1)); continue
    fi
    if python3 "$C6XDIFF" "$OUT/$name.obj" "$OUT/$name.hand.obj" > "$OUT/$name.diff"; then
        pass=$((pass + 1))
    else
        echo "FAIL $name: the driver's object is not asm6x's"
        sed 's/^/      /' "$OUT/$name.diff" | head -6
        fail=$((fail + 1))
    fi
done
echo "asm6x.sh: $pass objects through the driver as by hand, $fail failed, $refused the compiler refuses"
[ "$fail" -eq 0 ]
