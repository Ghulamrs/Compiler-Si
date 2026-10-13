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

**Debug information for the debuggers RIDE drives - reversed 2026-10-12.**
The decision of 2026-08-22 was "no `-g`, no DWARF, no CodeView, and none
planned". It is reversed: a Shalimar program in RIDE is to be debugged with
lldb, gdb and cdb as a c90 or cpp11 one is - breakpoints, steps, frames,
variables and watches. The reasoning and the plan are in
`docs/REVIEW-SHALIMAR-DEBUG-2026-10-12.md` in the cpp11 repository
(Ghulamrs/Compiler-Cpp-Optimize), and they come down to four rules:

- **The writers are cpp11's, not Shalimar-Codex's.** `Dwarf.cpp`,
  `CodeView.cpp` and `CodeViewTypes.cpp` are brought across and fed through
  an adapter from `Function::symbols_` and the four types; nothing of Codex's
  IR, `IRSelector` or `CoffWriter` comes in.
- **Codex's `DEBUGGER_RELEASE_REQUIREMENTS.md` is the acceptance contract**,
  verbatim: names, types and values right, array count and data shown
  honestly, scopes and shadowing honoured, nothing unavailable fabricated.
- **The rungs run D1 to D5**: lines on arm64-darwin and x86_64-linux, then
  variables and globals, then lexical blocks, then x86_64-windows through a
  GNU/COFF spelling and CodeView, then the C6747 once the house has a
  debugger there. Each green on all three boxes before the next, measured
  against clang or cl in the debugger as cpp11's M10 was.
- **`-g` adds sections and changes no code**, and `shm_line` and the session
  stay as they are: they are still what reports a runtime error's line and
  the only way to stop a program on the C6747.

D1 landed 2026-10-13 (below); RIDE's help and `toolchain.cpp` come down rung by
rung, not ahead of them.

## Rung D1: `-g`, lines and frames on arm64-darwin and x86_64-linux, 2026-10-13

**`shalimar -g` writes DWARF for the two GNU targets, and lldb and gdb stop a
Shalimar program on a line, step it and walk its stack.** Measured the way
cpp11's M10 was: `tests/m10/m10.py` puts the program under the host's debugger
beside clang's or gcc's build of the same program written in C line for line
(`tests/m10/w1.shl`, `w1.c`, one script `w1.script`), and compares where each
stopped, what the stack held and what the variables read. On the Mac, lldb: **17
agree, 0 differ**; on the Linux box, gdb 16.3: **15 agree, 0 differ** - a
breakpoint on line 9, `bt`, a step over, a step into `twice` and its stack
(`twice` at 2 over `main` at 10), a step out, `up` and the caller's variables.
`tests/m10.sh` is the suite, and it needs the host's C compiler and debugger.

**What was written, and where** - the plan of the review (section 5) held:

- `src/backend/DebugInfo.h` is the adapter: `DebugFunction` (name, symbol,
  unit and line, result type, `DebugLocal`s by slot, blocks), `DebugGlobal`
  by slot. `CodeGen::describe` fills one per function from
  `Function::symbols()` - the parameters first, a by-reference one marked
  `indirect` - and `Program::globalSymbols()`, which the checker now keeps.
- `src/backend/Dwarf.cpp` is cpp11's writer brought across and fed from the
  adapter: the abbreviations, the compile unit (DW_LANG_C99, so the C
  expression reader serves `p a->count`), a subprogram per function with
  DW_AT_frame_base the frame register, a parameter or variable per slot at
  `DW_OP_fbreg slotBias + 8*slot` (`DW_OP_deref` for a by-reference one), a
  variable per global at `shm_globals + 8*slot`, and the types - the four of
  them, an array being a pointer to a structure `{int32 count; int32
  element; data}` whose `data` points at the elements, which is exactly what
  the runtime's `Array` is. The line program is the assembler's, from `.file`
  and `.loc`.
- Each `Emitter` has four hooks that do nothing until `setDebugInfo`:
  `sourceLine` before a statement's first instruction - before the `shm_line`
  call, so the stop is where the statement begins - `describeFunction`,
  `describeGlobals`, and `beginBlock`/`endBlock` for D3. The two GNU emitters
  write `.file` per unit, `.loc unit+1 line`, `Lfunc.begin`/`Lfunc.end`
  labels, and `.cfi_startproc` / `.cfi_def_cfa` / `.cfi_offset` /
  `.cfi_endproc` round the prologue - the unwind data a stack walk needs,
  which neither target had. Slot k is at `rbp + shadow + 8k - frame` on
  x86-64 and `x29 + 8k - frame` on arm64; the emitter alone knows the frame,
  so it fills `slotBias` at `endFunction`.
- `-g` goes on the `c++` link line too: on a Mac that is what makes clang run
  `dsymutil`, so the DWARF outlives the object the driver assembled and threw
  away.

**`-g` adds sections and changes no code.** The emit golden - every program in
`tests/cases`, `tests/load`, `tests/debug` and `examples/` for all four
targets, 248 emissions, recorded before the first line of this was written -
reads **0 of 248 changed** without `-g`, at `-O0` and at `-O2`. The `.loc`
before a statement is a `raw` line, which flushes the x86 optimizer's run; it
cannot change what the peephole folds, because every statement already begins
with `shm_line`'s `call`, which ends a run, and no fold spans a statement.
`make test` 94/0, `tests/debug.sh` 19/0, `tests/tms6747.sh` 94/0 before and
after; the Linux box builds with g++ 11 and passes the same.

**Two things found by running it.** The `.cfi_*` lines went into *every*
prologue the moment `-g` was on, `shm_name_files` included, where no
`.cfi_startproc` had opened one, and clang refused the file: a function is
described only between `describeFunction` and the end of the function it
opened (`Emitter::describing()`). And `describing_` was cleared before the
prologue text was composed, so `twice`'s frame carried no CFA rule and lldb's
`bt` from inside it showed `twice` twice; the flag now outlives the insertion.

**The function is named as the program names it** - `main`, not
`shm_user_main` - so a stack reads `twice` over `main` over the runtime's
`main`, which has no source and ends the walk. `tests/debug.sh` and
`docs/DEBUGGING.md`'s session are untouched: `shm_line` is still emitted in
every build and is still what names a runtime error's line.

**What D1 does not claim**: variables and globals are D2, whose measurement is
the next section; the names are in the DWARF already because the adapter was
written whole, and `w1`'s `vars` lines agreeing with clang's is their first
sighting, not their proof. x86_64-windows and tms6747 are D4 and D5 - `-g` on
those targets writes nothing yet and says nothing, which D4 will mend.

## Rung D2: parameters, locals, globals and arrays, 2026-10-13

**A debugger shows a Shalimar program's names, types and values, writes one,
and reads an array through the structure it is.** `tests/m10/w2.shl` beside
`w2.c` - two globals, a by-reference parameter, a function over a `real[]`
with an `int`, and a `main` holding a `real[3]`, a `char[8]`, a `real[2][2]`,
an `int` and a `real` - under one script: a breakpoint inside the loop and two
in `main`, `vars` in each frame, the caller's variables from `up`, `print` of
`v->count`, `v->data[1]`, `m->count`, `m->data[1]->data[1]`, `t->data[1]`,
`t->count`, the two globals, the by-reference `n` inside `bump`, and `set
counter = 10` read back. **Mac, lldb: 37 agree, 0 differ. Linux box, gdb: 35
agree, 0 differ.** The C program's arrays are written as the runtime's
structure - `{int count; int element; double *data;}` and a pointer to it -
because that is what the Shalimar one *is*; a C array would have made the
debugger show an extent the Shalimar side has no business guessing. Codex's
contract rows - parameters, globals and locals by their Shalimar names with
correct types and values; array count and elements, the capacity (`count` 8
for `char t[8] : "hi"`) kept distinct from the text (`data` "hi"); nothing
unavailable fabricated - are met as far as a debugger can measure them.

**One line of DWARF changed, and dsymutil chose it.** A global's location was
`DW_OP_addr shm_globals + 8*slot`, and clang's assembler took it, gdb read it,
and `dsymutil` dropped every global but the first - `counter` was in the
dSYM and `scale`, at `_shm_globals + 8`, was not, so lldb called it undeclared.
It is `DW_OP_addr shm_globals` and then `DW_OP_plus_uconst 8*slot`: the
relocation names the symbol alone, which is what dsymutil keeps, and the
offset is an operation, which both debuggers evaluate. Measured on both
boxes; nothing else moved, and the emit golden still reads 0 of 248 without `-g`.

**Two things the measurement taught about Shalimar rather than about DWARF.**
A by-reference argument is copy-in, copy-out: `bump(counter)` evaluates the
global into a scratch slot, passes *that* address, and copies the result back
when the call returns - so at a `finish` out of `bump` the debugger reads
`counter` as 3, and 4 only once the statement has completed. That is the
language's semantics shown honestly, and the script stops at the next line to
read it. And stepping past a function's last statement leaves the function -
there is no line entry for the closing brace, as cpp11's M10 W5 records for
C++ - so a step that in C stops on `}` here returns to the caller.

**The harness learned two things from gdb.** It prints a pointer to a known
object as `0x404030 <vs>`, which normalises to `<pointer>` like lldb's bare
hex; and `info locals` lists the innermost block's variables first where
lldb's `frame variable` lists the outermost's, so one listing's `var` lines
are compared in name order. `print OURS | THEIRS` lets the two programs spell
one value differently - `n` in Shalimar, `*n` in C - under one label.

## Rung D3: lexical blocks, 2026-10-13

**A name made inside a block is the block's, and a debugger inside the block
reads that one.** `tests/m10/w3.shl` beside `w3.c`: `int i : 100`, a `for i`
whose counter hides it, a `t` made by first assignment inside the loop and
another `t` made after it, and a `u` inside an `if`. At line 5 `print i` is
0 and `print t` the loop's; at 12 they are 100 and 7; at 10 `vars` is `i`,
`t`, `u`. **Mac, lldb: 19 agree, 0 differ (the `vars` lists hold the hidden
names too, as C's do). Linux box, gdb: 19 agree, 0 differ.** No `vars` is
asked inside the loop, because the outer `t` is in scope there and not yet
set, in Shalimar and in C alike - the one value the two cannot agree on.

**The checker numbers the blocks and the generator puts the labels round
the same statements.** `Checker::openScope` asks `Function::openScope(parent)`
for a number each time `scope_` is pushed - a branch's body, an else, a
while's body, and for a `for` two: the counter's, round the whole loop, and the
body's inside it - and the `Symbol` a declaration or a first assignment
makes carries the number (`Symbol::scope`); a parameter is 0. The number goes
on the node (`If::Branch::scope`, `elseScope`, `While::scope`,
`For::counterScope`/`bodyScope`), so `CodeGen` can call `beginBlock` and
`endBlock` round exactly the statements the checker scoped, with no second
numbering to drift from the first. `Emitter::blockLabel` writes one label each
- `Lblock.<symbol>.<n>.b` and `.e`, `.L` on ELF - into the
`DebugFunction`'s block table, and `Dwarf.cpp` writes a `DW_TAG_lexical_block`
for every block that declares something, nested as the parents say; one that
declares nothing is flattened into its parent, as cpp11 does.

**Where a block's labels fall is why `-g` still changes no code at `-O2`.**
A branch body begins after its `jumpIfZero` and ends before the `jump` past
the else; a loop body sits between labels and jumps; the counter's block
begins after the statement's own `shm_line` call. Every one of those is
already where the x86 optimizer's run ends, so the `raw` label flushes a run
that was about to be flushed. The emit golden reads **0 of 248 changed**
without `-g` at `-O0` and at `-O2`.

**What Shalimar's rule makes of shadowing.** A declared name may not be
redeclared in an inner block, and a first assignment to a name that already
exists outside the block assigns the outer one - so a block-local name hides
an outer one only when the outer one is made *later* in the function, which is
what `w3` does with `t`, and a `for` counter always hides an `i` declared
before it. Those are the two shapes, and both resolve as the contract asks.

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
