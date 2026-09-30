# IDE projects for shc

The same layout as Compiler-Ci's and Compiler-Cppi's `ide/`, file for file: two
projects that build the whole compiler, every path in them `..` and one more
step. Nothing in the build depends on them - `make` and the suites are unchanged.

    shc.xcworkspace / shc.xcodeproj    Xcode, on macOS
    shc.sln / shc.vcxproj              Visual Studio 2022, on Windows
    shc.vcxproj.filters                the folder tree VS shows
    generate.py                        writes all five from the Makefile
    build-vs.cmd, check-vs.cmd         MSBuild and a smoke test, from a shell

**Two things the other two compilers' projects do not have.** A shalimar.exe with
no `lib/` beside it compiles, writes correct assembly and then dies at the link,
so both projects build the runtime after the compiler - the two host archives
from `runtime/`, as the Makefile's RUNTIME_SOURCES and DEBUG_RUNTIME_SOURCES
name them, and the C6000 runtime, `lib/shmrt-tms6747/*.s`, which is **cpp11's
output**. So each depends on cpp11: the Xcode project on
`../../Compiler-Cppi/ide/cxx1.xcodeproj`, which the workspace opens beside it,
and `shc.sln` holds `../../Compiler-Cppi/ide/cxx1.vcxproj` and builds it first.
The output is the solution's, `ide\x64\<config>\`, because the runtime step wants
cpp11.exe beside shalimar.exe.

**RIDE builds these very projects.** Until 2026-09-30 RIDE's
`tools/make-projects.py` wrote `shc.xcodeproj` and `shc.vcxproj` into the root
of this tree; `RIDE.sln` and `RIDE.xcworkspace` open these now, by the target
and product ids and the GUID RIDE derives from the program's name.

## Building

    ./generate.py                                      after a source is added to the Makefile
    ./generate.py --check                              says whether they are current
    xcodebuild -workspace shc.xcworkspace -scheme shc -configuration Release build
                                                       builds cpp11, shalimar.exe and lib/
    ide\build-vs.cmd [Debug]                           -> ide\x64\Release\shalimar.exe and lib\
    ide\check-vs.cmd                                   compiles examples\gcd.shm and runs it

`build.bat` at the root is the older command-line build, and is not these: it
still names the program shc.exe and lacks two of the Makefile's sources.
