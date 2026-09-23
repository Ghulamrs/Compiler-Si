#!/bin/sh
# tests/windows/ti-link.cmd on the box, from the Mac: every case compiled by the
# driver for tms6747 all the way to a TI program - asm6x on the assembly, lnk6x
# over the objects. The compiler, asm6x (and shalimar's runtime) are RIDE's, in its
# bin on the box, so the tree there is what RStudio's tools/to-windows.sh
# relayed last: this ships it again and rebuilds RIDE's solution first, unless
# told not to.
#   sh tests/ti-link.sh            relay, build, run
#   sh tests/ti-link.sh norelay    run against what is on the box
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME=$(basename "$ROOT")
BOX=${BOX:-windows}
RSTUDIO="$ROOT/../../RStudio"
if [ "${1:-}" != norelay ]; then
    [ -x "$RSTUDIO/tools/to-windows.sh" ] || { echo "ti-link.sh: no RStudio beside this tree to relay with"; exit 2; }
    ( cd "$RSTUDIO" && ./tools/to-windows.sh build > /dev/null ) || { echo "ti-link.sh: the relay failed"; exit 1; }
    ssh -n -o BatchMode=yes "$BOX" "cd /d C:\\Users\\GRA\\source\\RStudio & call build.bat solution" | grep -E "MISSING|error|built the solution" | head -5
fi
scp -q "$ROOT/tests/windows/ti-link.cmd" "$BOX:C:/Users/GRA/source/VM6747/$NAME/tests/windows/ti-link.cmd" || exit 1
ssh -n -o BatchMode=yes "$BOX" "C:\\Users\\GRA\\source\\VM6747\\$NAME\\tests\\windows\\ti-link.cmd C:\\Users\\GRA\\source\\VM6747\\$NAME C:\\Users\\GRA\\source\\RStudio\\bin" | tr -d '\r' | grep -v "^$"
