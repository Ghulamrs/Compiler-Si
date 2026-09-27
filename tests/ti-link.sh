#!/bin/sh
# tests/windows/ti-link.cmd on the box, from the Mac: every case compiled by the
# driver for tms6747 all the way to a TI program - asm6x on the assembly, lnk6x
# over the objects. The compiler, asm6x (and shalimar's runtime) are RIDE's, in its
# bin on the box: RIDE-4.5's tools/to-windows.sh relays the workspace to
# ED1_WINDOWS_ROOT (C:\ride-verify\win, the tree to-windows-both.sh builds) and
# builds the solution there first, unless told not to.
#   sh tests/ti-link.sh            relay, build, run
#   sh tests/ti-link.sh norelay    run against what is on the box
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME=$(basename "$ROOT")
BOX=${BOX:-windows}
RIDE="$ROOT/../../RIDE-4.5"
WROOT=${ED1_WINDOWS_ROOT:-'C:\ride-verify\win'}
if [ "${1:-}" != norelay ]; then
    [ -x "$RIDE/tools/to-windows.sh" ] || { echo "ti-link.sh: no RIDE-4.5 beside this tree to relay with"; exit 2; }
    ( cd "$RIDE" && ED1_WINDOWS_ROOT="$WROOT" ./tools/to-windows.sh solution > /dev/null ) || { echo "ti-link.sh: the relay failed"; exit 1; }
fi
F=$(printf '%s' "$WROOT" | sed 's|\\|/|g')     # printf: sh's echo reads \r as a carriage return
scp -q "$ROOT/tests/windows/ti-link.cmd" "$ROOT/tests/windows/par.cmd" "$BOX:$F/VM6747/$NAME/tests/windows/" || exit 1
ssh -n -o BatchMode=yes "$BOX" "$WROOT\\VM6747\\$NAME\\tests\\windows\\ti-link.cmd $WROOT\\VM6747\\$NAME $WROOT\\RIDE-4.5\\bin" | tr -d '\r' | grep -v "^$"
