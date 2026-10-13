#!/usr/bin/env python3
"""The debuggers, measured: a Shalimar program under lldb, gdb or cdb beside clang's or cl's
build of the same program written in C, line for line, and the two sessions compared.

    m10.py run     --debugger lldb|gdb|cdb PROGRAM SOURCE SCRIPT                 one session
    m10.py compare --debugger lldb|gdb|cdb OURS SOURCE THEIRS SOURCE SCRIPT      both, and the difference

SCRIPT is one command a line, in a vocabulary the three debuggers share; `@` in a breakpoint is
the program's own source file, so one script serves the Shalimar program and the C one:

    break @:LINE      run   next   step   finish   continue   up   down
                      (a step is repeated while it stays on the same line, further on)
    bt                the frames that have a source position, innermost first
    vars              the parameters and locals of the frame, by name and value
    print EXPR        one expression
    set VAR = VALUE   write a variable (then `print` it: the write itself says nothing)

Every stop is reported as `stop FILE:LINE FUNCTION` with the file's extension dropped, so a
Shalimar program and the C program beside it read the same; the backtrace ends at `main`, or at
the first frame the debugger has no source for, since what is above a program's main differs by
runtime, not by compiler.
Written for Compiler-Si's rungs D1 to D5 (docs/REVIEW-SHALIMAR-DEBUG-2026-10-12.md in cpp11),
on the lines of cpp11's M10 harness.
"""

import os
import re
import subprocess
import sys


def stem(path):
    return os.path.splitext(os.path.basename(path))[0]


def number(text):
    """A value, with a floating one rounded so that two debuggers' spellings of one double agree."""
    text = text.strip()
    m = re.match(r"^-?\d+\.\d+(e[-+]?\d+)?$", text)
    if m:
        return "%.9g" % float(text)
    m = re.match(r"^(-?\d+)\s+'(.*)'$", text)      # gdb's `65 'A'`
    if m:
        return "'" + m.group(2) + "'"
    m = re.match(r"^0n(-?\d+)$", text)             # cdb's `0n2`
    if m:
        return m.group(1)
    m = re.match(r"^0x([0-9a-f]+)`([0-9a-f]+)$", text)   # cdb's `0x000000ed`c2bbf8b0`
    if m:
        return "<pointer>" if int(m.group(1) + m.group(2), 16) != 0 else "<null>"
    m = re.match(r"^(\d+) '(.)'$", text)
    if m:
        return "'" + m.group(2) + "'"
    m = re.match(r"^0x([0-9a-fA-F]+)(?: <\S+>)?$", text)      # gdb's `0x404030 <vs>`
    if m:
        return "<pointer>" if int(m.group(1), 16) != 0 else "<null>"
    return text


# ---- the sessions ------------------------------------------------------------------------

def lldb_commands(script):
    out = []
    for line in script:
        words = line.split()
        if not words:
            continue
        c = words[0]
        if c == "break":
            out.append("breakpoint set --file %s --line %s" % tuple(words[1].split(":")))
        elif c == "run":
            out.append("process launch")
            out.append("frame info")
        elif c in ("next", "step", "finish", "continue", "up", "down"):
            out.append({"next": "thread step-over", "step": "thread step-in",
                        "finish": "thread step-out", "continue": "process continue",
                        "up": "frame select --relative 1", "down": "frame select --relative -1"}[c])
            if c not in ("up", "down"): out.append("frame info")
        elif c == "bt":
            out.append("thread backtrace")
        elif c == "vars":
            out.append("frame variable")
        elif c == "print":
            out.append("expression -- " + line[len("print"):].strip())
        elif c == "set":
            out.append("expression -- " + line[len("set"):].strip())
        else:
            raise SystemExit("m10: unknown command " + line)
    return out


def gdb_commands(script):
    out = []
    for line in script:
        words = line.split()
        if not words:
            continue
        c = words[0]
        if c == "break":
            out.append("break " + words[1])
        elif c in ("run", "next", "step", "finish", "continue", "up", "down"):
            out.append(c)
            if c not in ("up", "down"): out.append("frame")
        elif c == "bt":
            out.append("bt")
        elif c == "vars":
            out.append("info args")
            out.append("info locals")
        elif c == "print":
            out.append("print " + line[len("print"):].strip())
        elif c == "set":
            out.append("set var " + line[len("set"):].strip())
        else:
            raise SystemExit("m10: unknown command " + line)
    return out


def cdb_commands(script):
    out = []
    for line in script:
        words = line.split()
        if not words:
            continue
        c = words[0]
        if c == "break":
            f, l = words[1].split(":")
            out.append("bp `%s:%s`" % (f, l))
        elif c == "run":
            out.append("g")
            out.append(".frame")
        elif c in ("next", "step"):
            out.append({"next": "p", "step": "t"}[c])
            out.append(".frame")
        elif c == "finish":
            out.append("gu")
            out.append(".frame")
        elif c == "continue":
            out.append("g")
            out.append(".frame")
        elif c == "up":
            out.append(".f+")
        elif c == "down":
            out.append(".f-")
        elif c == "bt":
            out.append("k")
        elif c == "vars":
            out.append("dv /t")
        elif c == "print":
            out.append("?? " + line[len("print"):].strip())
        elif c == "set":
            out.append("?? " + line[len("set"):].strip())
        else:
            raise SystemExit("m10: unknown command " + line)
    return out


# `print OURS | THEIRS [| OURS-UNDER-CDB]`: the two programs spell one value differently - a
# by-reference parameter is `n` in Shalimar and `*n` in C, and under cdb, which shows a
# reference as the pointer it holds, `*n` for the Shalimar one too - and the line reads as the
# first spelling for both.
def side(line, which, debugger="lldb"):
    if "|" in line and line.split()[0] in ("print", "set"):
        word, rest = line.split(None, 1)
        parts = [p.strip() for p in rest.split("|")]
        ours, theirs = parts[0], parts[1]
        if which == 0 and debugger == "cdb" and len(parts) > 2:
            return word + " " + parts[2], ours
        return word + " " + (ours if which == 0 else theirs), ours
    return line, None


# **cdb is driven a command at a time**, over its own stdin, rather than from one `-c` line: `??`
# takes the rest of a line as its expression, and a step has to be judged before the next command
# is chosen. A step is repeated while it leaves the program on the same line, further on - the
# rule RIDE's Debugger applies (help/08-debugging.md, "A step that appears to do nothing") - so
# that `t` over the runtime calls a Shalimar statement begins with reaches the callee, as lldb's
# and gdb's step past a function without lines does.
class Cdb:
    PROMPT = re.compile(r"(?:\d+:\d{3}|NoTarget)> $")

    def __init__(self, program):
        self.p = subprocess.Popen(["cdb", "-G", "-lines", program], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.raw = ""
        self.readToPrompt()

    def readToPrompt(self):
        got = b""
        while True:
            chunk = self.p.stdout.read1(4096) if hasattr(self.p.stdout, "read1") else self.p.stdout.read(1)
            if not chunk:
                break
            got += chunk
            text = got.decode("utf-8", "replace")
            if self.PROMPT.search(text):
                break
        text = got.decode("utf-8", "replace").replace("\r", "")
        self.raw += text
        return self.PROMPT.sub("", text)

    def cmd(self, c):
        try:
            self.p.stdin.write((c + "\n").encode("utf-8"))
            self.p.stdin.flush()
        except (BrokenPipeError, OSError):
            return ""
        return self.readToPrompt()

    def quit(self):
        try:
            self.cmd("q")
        except Exception:
            pass
        try:
            self.p.wait(timeout=10)
        except Exception:
            self.p.kill()


def cdb_place(text):
    """(function, file, line, offset) of the frame `.frame` printed, or None."""
    m = FRAME_CDB.search(text)
    if not m:
        return None
    off = re.search(r"\+0x([0-9a-f]+)", text)
    return (m.group(1), m.group(2), m.group(3), int(off.group(1), 16) if off else 0)


def cdb_session(program, script, marker):
    d = Cdb(program)
    blocks = []
    def run(c):
        out = d.cmd(c)
        blocks.append([c, out.splitlines()])
        return out
    d.cmd(".lines -e")
    d.cmd("l+t")
    d.cmd('.step_filter "*!shm_*;*!shm::*"')
    # And the CRT's own code, which the symbol server gives lines to: a runtime call's
    # __security_check_cookie is not where a step into a Shalimar function should land.
    d.cmd(".settings set Sources.SkipCrtCode = true")
    for line in script:
        words = line.split()
        if not words:
            continue
        c = words[0]
        if c == "break":
            f, l = words[1].split(":")
            run("bp `%s:%s`" % (f, l))
        elif c in ("run", "continue"):
            run("g")
            run(".frame")
        elif c in ("next", "step"):
            before = cdb_place(d.cmd(".frame"))
            where = ""
            for attempt in range(12):
                run("p" if c == "next" else "t")
                where = d.cmd(".frame")
                after = cdb_place(where)
                if before is None or after is None:
                    break
                if after[:3] == before[:3] and after[3] > before[3]:
                    continue
                # **Into a function, once more**: cdb stops on the callee's own line, before its
                # parameters are stored, where lldb and gdb stop on its first statement; RIDE's F6
                # steps once more from such a stop (cpp11's M10 W5), and so does this.
                if c == "step" and after[0] != before[0]:
                    run("p")
                    where = d.cmd(".frame")
                break
            blocks.append([".frame", where.splitlines()])
        elif c == "finish":
            run("gu")
            run(".frame")
        elif c == "up":
            run(".f+")
        elif c == "down":
            run(".f-")
        elif c == "bt":
            run("k")
        elif c == "vars":
            run("dv /t")
        elif c in ("print", "set"):
            run("?? " + line[len(c):].strip())
        else:
            raise SystemExit("m10: unknown command " + line)
    d.quit()
    return d.raw, blocks


def run_session(debugger, program, source, script, which=0):
    labels = {}
    chosen = []
    for line in script:
        line, label = side(line, which, debugger)
        if label is not None:
            labels[line.split(None, 1)[1].strip()] = label
        chosen.append(line)
    script = [line.replace("@", os.path.basename(source)) for line in chosen]
    marker = "@@m10@@"
    if debugger == "lldb":
        cmds = lldb_commands(script)
        args = ["lldb", "--batch", "--no-lldbinit", "-o", "settings set auto-confirm true"]
        for c in cmds:
            args += ["-o", "script print('%s %s')" % (marker, c), "-o", c]
        args += ["-o", "process kill", "--", program]
    elif debugger == "gdb":
        cmds = gdb_commands(script)
        args = ["gdb", "-nx", "-batch", "-ex", "set confirm off", "-ex", "set width 0",
                "-ex", "set print pretty off"]
        for c in cmds:
            args += ["-ex", "echo %s %s\\n" % (marker, c), "-ex", c]
        args += ["-ex", "kill", "--args", program]
    elif debugger == "cdb":
        raw, blocks = cdb_session(program, script, marker)
        return raw, blocks, labels
    else:
        raise SystemExit("m10: no such debugger " + debugger)
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    raw = p.stdout.decode("utf-8", "replace")
    # One block per command, headed by the marker line that named it.
    blocks = []
    current = None
    for line in raw.splitlines():
        if line.startswith("(lldb) script") or line.startswith("(gdb) echo"):
            continue      # the command that prints the marker, echoed, is not the marker
        if marker in line:
            current = [line.split(marker, 1)[1].strip(), []]
            blocks.append(current)
        elif current is not None:
            current[1].append(line)
    return raw, blocks, labels


# ---- normalising what each debugger said ---------------------------------------------------

FRAME_LLDB = re.compile(r"frame #(\d+): 0x[0-9a-f]+ \S+?`(\S+?)(?:\(.*?\))? at ([^:]+):(\d+)")
FRAME_GDB = re.compile(r"#(\d+)\s+(?:0x[0-9a-f]+ in )?(\S+) \(.*?\) at ([^:]+):(\d+)")
# `k`: `000000ed`c2bbf8b0 00007ff7`3d65cab0     w1!main+0x69 [C:\...\w1.shl @ 9]`; `.frame` has a number in front.
FRAME_CDB = re.compile(r"^(?:[0-9a-f]+ )?[0-9a-f`]+ [0-9a-f`]+\s+(?:\S+!)?(\S+?)(?:\+0x[0-9a-f]+)? \[(.*?) @ (\d+)\]")
FRAME_CDB_ONE = FRAME_CDB
FRAME_CDB_ANY = re.compile(r"^(?:[0-9a-f]+ )?[0-9a-f`]+ [0-9a-f`]+\s+\S")


def sortedVars(out, start):
    """The variables listed since `start`, in name order: gdb lists the innermost block's first and lldb the outermost's."""
    out[start:] = sorted(out[start:])


def normalise(debugger, blocks, labels=None):
    labels = labels or {}
    out = []
    for command, lines in blocks:
        text = "\n".join(lines)
        word = command.split()[0]
        if out and out[-1] == "exited" and command in ("frame info", "frame", ".frame"):
            continue
        if debugger == "lldb":
            if command == "frame info" or command.startswith("frame select"):
                m = FRAME_LLDB.search(text)
                if m:
                    out.append("stop %s:%s %s" % (stem(m.group(3)), m.group(4), m.group(2)))
                elif "exited with status" in text or "exited" in text:
                    out.append("exited")
                else:
                    out.append("stop <no source>")
            elif command == "thread backtrace":
                n = 0
                for line in lines:
                    if "frame #" not in line:
                        continue
                    m = FRAME_LLDB.search(line)
                    if not m:
                        break
                    out.append("frame %d %s %s:%s" % (n, m.group(2), stem(m.group(3)), m.group(4)))
                    n += 1
                    if m.group(2) == "main": break
            elif command == "frame variable":
                start = len(out)
                for line in lines:
                    m = re.match(r"^\((.*?)\) (\S+) = (.*)$", line)
                    if m:
                        out.append("var %s = %s" % (m.group(2), number(m.group(3))))
                sortedVars(out, start)
            elif command.startswith("expression"):
                expr = command[len("expression --"):].strip()
                m = re.search(r"\$\d+ = (.*)$", text)
                if "=" in expr and not re.search(r"[=!<>]=", expr):
                    continue
                out.append("print %s = %s" % (labels.get(expr, expr), number(m.group(1)) if m else "<error>"))
            elif "exited with status" in text:
                out.append("exited")
        elif debugger == "gdb":
            if command == "frame":
                m = FRAME_GDB.search(text)
                if m:
                    out.append("stop %s:%s %s" % (stem(m.group(3)), m.group(4), m.group(2)))
                elif "No stack" in text:
                    out.append("exited")
                else:
                    out.append("stop <no source>")
            elif command == "bt":
                n = 0
                for line in lines:
                    if not line.startswith("#"):
                        continue
                    m = FRAME_GDB.search(line)
                    if not m:
                        break
                    out.append("frame %d %s %s:%s" % (n, m.group(2), stem(m.group(3)), m.group(4)))
                    n += 1
                    if m.group(2) == "main": break
            elif command in ("info args", "info locals"):
                start = len(out)
                for line in lines:
                    m = re.match(r"^(\S+) = (.*)$", line)
                    if m:
                        out.append("var %s = %s" % (m.group(1), number(m.group(2))))
                # args then locals are two listings; sorted together once the locals are in
                if command == "info locals":
                    back = start
                    while back > 0 and out[back - 1].startswith("var "):
                        back -= 1
                    sortedVars(out, back)
            elif word == "print":
                expr = command[len("print"):].strip()
                m = re.search(r"\$\d+ = (.*)$", text)
                out.append("print %s = %s" % (labels.get(expr, expr), number(m.group(1)) if m else "<error>"))
            elif word in ("run", "continue", "next", "step", "finish") and "exited" in text:
                out.append("exited")
        elif debugger == "cdb":
            if command == ".frame" or command in (".f+", ".f-"):
                m = FRAME_CDB_ONE.search(text)
                if m:
                    out.append("stop %s:%s %s" % (stem(m.group(2)), m.group(3), m.group(1)))
                elif "exited" in text.lower() or "No runnable debuggees" in text:
                    out.append("exited")
                else:
                    out.append("stop <no source>")
            elif command == "k":
                n = 0
                for line in lines:
                    m = FRAME_CDB.match(line)
                    if not m:
                        if FRAME_CDB_ANY.match(line):
                            break
                        continue
                    out.append("frame %d %s %s:%s" % (n, m.group(1), stem(m.group(2)), m.group(3)))
                    n += 1
                    if m.group(1) == "main": break
            elif command in ("g", "p", "t", "gu") and ("exited" in text.lower() or "No runnable debuggees" in text):
                out.append("exited")
            elif command == "dv /t":
                start = len(out)
                for line in lines:
                    m = re.match(r"^\s*(?:struct |class )?\S+\s*\*?\s+(\S+) = (.*)$", line.strip())
                    if m:
                        out.append("var %s = %s" % (m.group(1), number(m.group(2))))
                sortedVars(out, start)
            elif command.startswith("??"):
                expr = command[2:].strip()
                if "=" in expr and not re.search(r"[=!<>]=", expr):
                    continue
                m = re.search(r"(?:^|\n)(?:\S+ )?(\S+)\s*$", text.strip())
                value = text.strip().split("\n")[-1] if text.strip() else "<error>"
                value = re.sub(r"^(int|double|unsigned char|char)\s+", "", value)
                out.append("print %s = %s" % (labels.get(expr, expr), number(value)))
    return out


def main(argv):
    if len(argv) < 2 or argv[0] not in ("run", "compare"):
        print(__doc__)
        return 2
    debugger = "lldb"
    args = argv[1:]
    if args and args[0] == "--debugger":
        debugger = args[1]
        args = args[2:]
    if argv[0] == "run":
        program, source, script = args[0], args[1], args[2]
        with open(script) as f:
            lines = [l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")]
        raw, blocks, labels = run_session(debugger, program, source, lines,
                                          1 if os.environ.get("M10_THEIRS") else 0)
        if os.environ.get("M10_RAW"):
            print(raw)
        for line in normalise(debugger, blocks, labels):
            print(line)
        return 0
    ours, oursrc, theirs, theirsrc, script = args[0], args[1], args[2], args[3], args[4]
    with open(script) as f:
        lines = [l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")]
    ra = run_session(debugger, ours, oursrc, lines, 0)
    rb = run_session(debugger, theirs, theirsrc, lines, 1)
    a = normalise(debugger, ra[1], ra[2])
    b = normalise(debugger, rb[1], rb[2])
    same = 0
    differ = 0
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else "<nothing>"
        y = b[i] if i < len(b) else "<nothing>"
        if x == y:
            same += 1
            print("  " + x)
        else:
            differ += 1
            print("- " + x)
            print("+ " + y)
    print("%s: %d agree, %d differ (%s against %s)" % (debugger, same, differ, ours, theirs))
    return 0 if differ == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
