# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

A compiler for **Shalimar**, the small numeric language the iOS app in
`../Shalimar` interprets. It reads a `.shm` program and writes native
assembly for three targets: `arm64-darwin`, `x86_64-linux` and
`x86_64-windows`.

**`../Shalimar/SHALIMAR_LANGUAGE.md` is the specification.** Where this
compiler and that document disagree, the document wins and this compiler is
what gets fixed. Where the *app's interpreter* and the document disagree, the
document still wins - the interpreter is the oracle the tests are recorded
from, not the authority. Every such divergence is written down in
`docs/CONFORMANCE.md`; nothing is left as an undocumented difference.

## The language this is written in

`src/` and `runtime/` are **ISO C++14**, and anything added to them must be
too. All three toolchains pin it: `-std=c++14 -Wall -Wextra -Werror
-pedantic` in the `Makefile`, `/std:c++14 /W4 /WX` for `cl`.

**A Mac cannot enforce this.** Apple's libc++ hands you C++17 names under
`-std=c++14`, so a C++17-ism compiles clean here, passes the host suite, and
is refused only when it reaches real g++. `./tests/remote-linux.sh` builds
with g++ 11 on the Linux box, which is what actually says whether the sources
are C++14. Run it before believing a change is finished.

One more thing a Mac will not catch: **trigraphs are still live in C++14**.
`"use '??' or ..."` in a string literal becomes `use '^ ...` under
`-pedantic`. Write `'?\?'`.

## Build and test

```
make                      build shc and the host runtime
./tests/run.sh            the host suite
./tests/remote-linux.sh   build with real g++ and run the suite on the box
./tests/remote-windows.sh assemble with ml64 and run on the Windows box
./tests/record.sh         re-record expected output from the app's interpreter
./tests/cross.sh          finding the rest of the program in the other files
./tests/debug.sh          stopping a program from inside itself
./tests/linking.sh        what an object holds, and why it is a whole program
./tests/build-windows.sh  build shc itself on the Windows box, with cl
```

`build.bat` is the MSVC build, and `cl` is the third toolchain the C++14 claim
has to hold under. On that host the driver names `ml64` and `link` rather than
`c++`, so shc there needs the Visual Studio environment at run time too.

A case is `tests/cases/<name>.shm` beside `tests/cases/<name>.expected`. The
expected file is what the app's interpreter printed, recorded by
`tests/record.sh` on a Mac and committed, so the other two machines need
neither Swift nor the app's checkout.

## Architecture

```
Driver -> tokenize() -> Parser -> Checker -> CodeGen -> Emitter -> assembly
```

**`CodeGen` is one walk of the tree and there are three `Emitter`s.** That is
the opposite of giving each target its own walk, and it is deliberate: the
order values are evaluated in, where a jump goes, which runtime entry point a
type reaches are all the same on every machine and are written once.
Everything a target may differ over is a virtual method on `Emitter`.

**Three things vary per target and they are separate axes.** `Abi` answers
how arguments travel; `Spelling` answers how an instruction is written down;
the target class answers what the assembler wants around the instructions.
One instruction stream serves both x86-64 targets - `GnuSpelling` and
`MasmSpelling` are the whole of what differs between them - so keeping
"Windows" from becoming a synonym for "Intel syntax" is a live concern, not a
tidiness one.

**The runtime does the work that is not arithmetic or control flow.**
`runtime/Runtime.cpp` owns printing, and will own arrays, strings and
diagnostics. It is C++14 with C linkage, compiled once per platform, and it
also owns `main()`: the compiler emits Shalimar's `main` as `shm_user_main`
so the C entry point stays the runtime's.

## Decisions that are settled

**No debugger support.** No `-g`, no DWARF, no CodeView, and none planned -
asked and answered on 2026-08-22. What stands in for it is `shm_line`, which
lets a runtime error name its line and its function, and `-S`. Extend those
if a program is hard to see into; do not start a line table.

## How this is built

**One language feature at a time, green on all three targets before the
next.** The skeleton came up as `? 1 2 3` compiled and run natively on all
three machines before anything else was written, and each feature since has
gone in the same way. A backend that is brought up late is a backend that is
ported rather than written, and the two do not produce the same code.

## Verification

**Prove the artefact, not the exit status.** A green suite proves nothing
until you know what it ran against - `make` with nothing to rebuild, or a
relayed tree that never updated, will both report success.

- `[ shc -nt src/Parser.cpp ]` before believing a remote run
- **a stale object file is a heap corruptor, not a link error.** The Makefile
  carries `-MMD -MP` for this reason. Without header dependencies an edited
  header rebuilds only some translation units, the rest keep the previous
  definition of a class, the link succeeds because the mangled names still
  match, and the program corrupts its heap somewhere unrelated. A sanitiser
  build cannot reproduce it - a sanitiser build is a clean build. The remote
  suites therefore build from clean, because a relayed tree is not a
  checked-out one.
- grep the emitted assembly for a token the change introduces; it is the
  cheapest confirmation there is
- when a suite fails on one host and passes on another, **suspect the host's
  tools before the compiler**. `timeout` is GNU coreutils and absent on macOS

## The machines

| Machine | Reached by | What it is for |
| --- | --- | --- |
| this Mac | - | writing, `arm64-darwin` natively |
| Linux box | `ssh -i ~/Documents/Claude/myMorningWalk.pem ec2-user@52.202.164.123` | real g++, `x86_64-linux` natively |
| Windows box | `ssh windows` | `ml64` and `link`, `x86_64-windows` natively |

**The Windows ssh shell is `cmd`**, since that box was rebuilt on 2026-08-25.
It was PowerShell before, and both remote scripts assumed so. `git` is not on
its `PATH` either way, and nested quotes still mangle, so the far side runs a
`.bat` - but **named by its full path and nothing else**:

```
ssh windows "C:\shalimar\build.bat sort"          # runs under either shell
ssh windows "cmd /c C:\shalimar\build.bat sort"   # nests cmd in cmd
ssh windows "cd DIR; cmd /c build.bat"             # ';' is not a cmd separator
```

The second nests `cmd` inside `cmd` and a quote leaks into the batch file's
`%1` - `build.bat sort` arrives as `sort"`, which fails the first `if` in the
script. That read as 57 compiler failures and was a shell quoting fault. The
third silently did nothing at all.

The scaffold those scripts drive - `tests/windows/*.bat` - is **in this
repository**, and is copied over on every run. It used to live only on the box,
so the rebuild took it and the suite then failed with `remote mkdir: No such
file or directory`, which reads as a network fault. A freshly installed box now
needs nothing done to it by hand.

**The four projects are siblings under `~/source` on that box** - `RStudio`,
`Compiler-C`, `Compiler-S`, `Converter-C2S` - because `RStudio.sln` names
`..\Compiler-C\msvc\cc1.vcxproj`. Build into that tree and no other: a second
copy elsewhere ages apart from the one every other tool there reads, and then
passes.

## Comments cut to the cap, 2026-09-26: the long forms

**No comment group in `src/` or `runtime/` runs longer than three lines, and
one standing in front of a single line of code runs one line** - the house
rule, with `C++Optimize/tools/comment-lines` as its oracle. The 47 groups over
it were cut with whole sentences kept and the finding first; the library facts
they carried - `fabs` and `OPTION NOKEYWORD`, `abs_int` and the NaN-propagating
`max`/`min`, `-lm` named rather than relied upon - were already in
`docs/FOREIGN.md` and are not repeated. What follows is the rest worth finding
without `git log -p`.

**1.2 is the first version this compiler has had a number for** (`Driver.cpp`,
`shcVersion`). It had none until 2026-08-26 - built, relayed and run without
one, which works until somebody has two copies and needs to say which is which.
It is 1.2 rather than 1.0 because it is numbered alongside RStudio, which drives
it: an editor at 1.2 driving a compiler at 1.0 invites the question of which
pairs with which, and there is only ever one answer here. The releases they
share are what the number tracks - 1.2 being `uses`, the borrowed library and
the foreign declaration. `--version` and `--help` leave with 0 and a bad
argument with 2; both used to be 2, which is why a script asking three compilers
their versions stopped at the second one.

**A foreign declaration with two outputs is refused, and it is not a limitation
to be lifted** (`Check.cpp`, the walk over `program.foreign()`). shc returns two
outputs through a scratch block whose address it passes in a register of its own
choosing - a convention that is fine while both ends are code this compiler
wrote, and is not written down anywhere for anybody else to implement. Such a
declaration parses, emits and links, and would simply be wrong, which is the
worst of the four outcomes. A C function returning two values does it through a
pointer parameter, and Shalimar has no pointer type - so there is no spelling of
this that would work, and refusing is the whole answer.

**A program may have its own `pi` or `e`, but it has to say so**
(`Check.cpp`). Declared - `real pi`, or a parameter - the name is the program's
for that whole body and the constant is simply not in it. Created by assignment
it is refused, because Shalimar makes a name on first write: `pi : 3` would
leave `? pi` meaning 3.14159 above the line and 3 below it, one name with two
meanings in one function. That is the hazard SHALIMAR_LANGUAGE.md named when it
made these read-only, and it is the half worth keeping.

**A borrowed name may not also be a variable, and the check carries on**
(`Check.cpp`, `refuseBorrowed`; FOREIGN.md rule 3). `fmod` is an ordinary
identifier in every file that does not borrow it; in one that does, `real fmod`
beside `fmod(7.5, 2.0)` would be one name meaning two things in one file. It is
stricter than a constant, which may be had by declaring it: there is no
declaring your way out of a borrow, because the clause has already claimed the
name for the file, so the message names both lines. Only this file's own borrows
count - Resolve merges the borrows of any file it pulls a function from, so that
the pulled function's calls resolve, and those must not take a name away from a
variable here; `Program::Borrowed::own` is what tells them apart, and without it
a `uses fmod` in a file you merely call into would refuse `fmod` as a variable
here, where the app - one file, no merging - accepts it. Every caller reports
and carries on rather than returning, so the name still enters scope: bailing
produced "'fmod' is borrowed" followed by "Undefined variable 'fmod'" at a line
that is not the mistake.

**A redeclared name is declared anyway** (`Check.cpp`, `visit(Declaration)`).
Returning after the report left the name undefined, so every later mention drew
a second "Undefined variable" - one mistake, two messages, and the reader sent
to the wrong line. The app's interpreter reports the redeclaration alone, and
the differential suite caught the difference the moment a case used the name
after declaring it twice.

**`declaredLocals_` exists because `scope_` cannot answer for sibling blocks**
(`Check.h`). A declaration may sit in a block now, but a declared local is still
the whole call's - one name, one variable, one type - so two sibling blocks may
not each declare `t`. The first block's level is popped long before the second
is read, so a set kept for the whole function is what remembers it. Names made
by a first assignment are not in it; those belong to their block and always
have. The same rule is why a declaration inside an `if` may not shadow one
outside it, and why `Checker::visit(Declaration)` asks `lookup` rather than
`definedHere`.

**Declarations go wherever a statement goes** (`Parser.cpp`). They were refused
below the top of a function body once; the rule went so that a C program keeps
its shape when it is converted rather than having every local hoisted to the
top. What did not go is the lifetime - §6 of the specification.

**`uses` is taken by the parser and judged by the checker** (`Parser.cpp`,
`parseUses`). Whether a name is borrowable is a question about the table rather
than about the grammar, and a parser that answered it would have to carry the
table. The comma is required between names and forbidden after the last one,
the parameter list's rule, so that `uses sin,` reads as a mistake rather than as
a name that has not been typed yet.

**`--with=` names a library on the command line rather than in the source**
(`Driver.cpp`) for the reason C splits a header from `-l`: the program says what
it calls, the build says where that lives, and the same source then serves a
machine where the library sits somewhere else. A declaration with no library
named is refused before the link, once, and only when linking: `-c` and `-S`
produce an object somebody else will link, who is entitled to bring the library.

**The C99 rows in the builtin table** (`Builtin.cpp`) were added 2026-08-26, and
each is a row and nothing else: the symbol is libm's, so there is no wrapper to
write and the archive does not grow. `log2`, `cbrt` and the hyperbolics postdate
the standard Compiler-C targets, but this is a call into the platform's libm,
not a C program, and all three of ours have them. The unborrowable list beside
it is short on purpose: it turns the most likely mistakes into instructions
naming the type Shalimar lacks, and anything not on it still gets a plain "not
a library function this compiler knows", which is true and not misleading.
