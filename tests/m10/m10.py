#!/usr/bin/env python3
"""The debuggers, measured: a Shalimar program under lldb, gdb or cdb beside clang's or cl's
build of the same program written in C, line for line, and the two sessions compared.

    m10.py run     --debugger lldb|gdb|cdb PROGRAM SOURCE SCRIPT                 one session
    m10.py compare --debugger lldb|gdb|cdb OURS SOURCE THEIRS SOURCE SCRIPT      both, and the difference

SCRIPT is one command a line, in a vocabulary the three debuggers share; `@` in a breakpoint is
the program's own source file, so one script serves the Shalimar program and the C one:

    break @:LINE      run   next   step   finish   continue   up   down
    bt                the frames that have a source position, innermost first
    vars              the parameters and locals of the frame, by name and value
    print EXPR        one expression
    set VAR = VALUE   write a variable (then `print` it: the write itself says nothing)

Every stop is reported as `stop FILE:LINE FUNCTION` with the file's extension dropped, so a
Shalimar program and the C program beside it read the same; a frame the debugger has no source
for ends the backtrace, since what is above a program's main differs by runtime, not by compiler.
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
    if re.match(r"^0x[0-9a-fA-F]+$", text):
        return "<pointer>" if text != "0x0" and text != "0x0000000000000000" else "<null>"
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


def run_session(debugger, program, source, script):
    script = [line.replace("@", os.path.basename(source)) for line in script]
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
        cmds = cdb_commands(script)
        text = ""
        for c in cmds:
            text += ".echo %s %s;%s;" % (marker, c, c)
        text += "q"
        args = ["cdb", "-G", "-lines", "-c", text, program]
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
    return raw, blocks


# ---- normalising what each debugger said ---------------------------------------------------

FRAME_LLDB = re.compile(r"frame #(\d+): 0x[0-9a-f]+ \S+?`(\S+?)(?:\(.*?\))? at ([^:]+):(\d+)")
FRAME_GDB = re.compile(r"#(\d+)\s+(?:0x[0-9a-f]+ in )?(\S+) \(.*?\) at ([^:]+):(\d+)")
FRAME_CDB = re.compile(r"^([0-9a-f]+) [0-9a-f`]+ [0-9a-f`]+ (?:\S+!)?(\S+?)(?:\+0x[0-9a-f]+)? \[(.*?) @ (\d+)\]")
FRAME_CDB_ONE = re.compile(r"^([0-9a-f]+) [0-9a-f`]+ [0-9a-f`]+ (?:\S+!)?(\S+?)(?:\+0x[0-9a-f]+)? \[(.*?) @ (\d+)\]")


def normalise(debugger, blocks):
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
            elif command == "frame variable":
                for line in lines:
                    m = re.match(r"^\((.*?)\) (\S+) = (.*)$", line)
                    if m:
                        out.append("var %s = %s" % (m.group(2), number(m.group(3))))
            elif command.startswith("expression"):
                expr = command[len("expression --"):].strip()
                m = re.search(r"\$\d+ = (.*)$", text)
                if "=" in expr and not re.search(r"[=!<>]=", expr):
                    continue
                out.append("print %s = %s" % (expr, number(m.group(1)) if m else "<error>"))
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
            elif command in ("info args", "info locals"):
                for line in lines:
                    m = re.match(r"^(\S+) = (.*)$", line)
                    if m:
                        out.append("var %s = %s" % (m.group(1), number(m.group(2))))
            elif word == "print":
                expr = command[len("print"):].strip()
                m = re.search(r"\$\d+ = (.*)$", text)
                out.append("print %s = %s" % (expr, number(m.group(1)) if m else "<error>"))
            elif word in ("run", "continue", "next", "step", "finish") and "exited" in text:
                out.append("exited")
        elif debugger == "cdb":
            if command == ".frame" or command in (".f+", ".f-"):
                m = FRAME_CDB_ONE.search(text)
                if m:
                    out.append("stop %s:%s %s" % (stem(m.group(3)), m.group(4), m.group(2)))
                elif "exit" in text.lower():
                    out.append("exited")
                else:
                    out.append("stop <no source>")
            elif command == "k":
                n = 0
                for line in lines:
                    m = FRAME_CDB.match(line)
                    if not m:
                        if re.match(r"^[0-9a-f]+ [0-9a-f`]+ [0-9a-f`]+ ", line):
                            break
                        continue
                    out.append("frame %d %s %s:%s" % (n, m.group(2), stem(m.group(3)), m.group(4)))
                    n += 1
            elif command == "dv /t":
                for line in lines:
                    m = re.match(r"^\s*(?:struct |class )?\S+\s*\*?\s+(\S+) = (.*)$", line.strip())
                    if m:
                        out.append("var %s = %s" % (m.group(1), number(m.group(2))))
            elif command.startswith("??"):
                expr = command[2:].strip()
                if "=" in expr and not re.search(r"[=!<>]=", expr):
                    continue
                m = re.search(r"(?:^|\n)(?:\S+ )?(\S+)\s*$", text.strip())
                value = text.strip().split("\n")[-1] if text.strip() else "<error>"
                value = re.sub(r"^(int|double|unsigned char|char)\s+", "", value)
                out.append("print %s = %s" % (expr, number(value)))
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
        raw, blocks = run_session(debugger, program, source, lines)
        if os.environ.get("M10_RAW"):
            print(raw)
        for line in normalise(debugger, blocks):
            print(line)
        return 0
    ours, oursrc, theirs, theirsrc, script = args[0], args[1], args[2], args[3], args[4]
    with open(script) as f:
        lines = [l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")]
    a = normalise(debugger, run_session(debugger, ours, oursrc, lines)[1])
    b = normalise(debugger, run_session(debugger, theirs, theirsrc, lines)[1])
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
