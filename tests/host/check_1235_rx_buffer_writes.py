#!/usr/bin/env python3
"""Pin the #1235 r5 buffer-reuse invariant in drv_sdspi.c.

While a filler-acceptance outcome is unresolved, nothing may clear or overwrite
the command-response buffer (pCmdResp) before what the previous resync
transfer left there was scanned for the token -- including bytes an ABORTED or
LATE transfer delivered. The driver's answer is ONE guarded helper,
lDRV_SDSPI_WriteResyncRxReset(): it scans what is there (inherited), then
clears, then records what the next transfer will receive. This pin finds EVERY
write into pCmdResp in the source and requires the helper:

  1. Every store into pCmdResp inside the resync path (any function named
     lDRV_SDSPI_WriteResync*) must be preceded, in the same or an enclosing
     block and without crossing a `break;` or a `case` label, by a call to the
     helper. A store is: `pCmdResp[..] = ..`, `memset(..pCmdResp..)`, or a
     transfer that RECEIVES into it (DRV_SDSPI_SPIRead, or the third argument of
     DRV_SDSPI_SPIWriteRead). Only the helper itself may memset it.
  2. The helper begins with the inherited scan, before it clears.
  3. Every function OUTSIDE the resync path that writes pCmdResp is on the
     documented list below, and the list is exact: a new writer fails this pin
     until someone decides, in this file, why it cannot run while an acceptance
     is unresolved.
       - lDRV_SDSPI_CommandSend: FRAME_PACKET diverts to RESYNC_START while
         writeInDoubt != NONE BEFORE its first store (checked below), and the
         response reads follow a command this FSM sent.
       - lDRV_SDSPI_MediaInitialize and lDRV_SDSPI_BufferIOTasks: the
         media-init and block-IO states. Each runs only after a command of the
         same request completed, and a command first resolves any write in
         doubt (CommandSend, above).
  4. DRV_SDSPI_AbortedXferRecord marks the receive buffer late when DRV_SPI still
     holds the aborted transfer, so a scan that ran at the abort does not retire
     bytes that land afterwards. (DRV_SDSPI_ReleaseBus needs no scan of its own:
     it leaves resyncRxUnscanned set and the next helper call looks.)

A text pin over one file (the #1033 class): it catches a deleted or moved call
or a new unguarded writer, not a determined obfuscation. `--self-test` proves
each check can fail.

Run: python3 tests/host/check_1235_rx_buffer_writes.py [--source F] [--self-test]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDIR = ROOT / "firmware/src/config/default/driver/sdspi/src"
DEFAULT = SDIR / "drv_sdspi.c"
IFACE = SDIR / "drv_sdspi_driver_interface.c"

HELPER = "lDRV_SDSPI_WriteResyncRxReset"
OUTSIDE_ALLOWED = {"lDRV_SDSPI_CommandSend", "lDRV_SDSPI_BufferIOTasks", "lDRV_SDSPI_MediaInitialize"}


def strip(src):
    """Blank comments and string/char literal contents, keeping offsets."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * (j - i - 2) + c if j - i >= 2 else c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def functions(text):
    """[(name, body_start, body_end)] for the top-level function definitions."""
    res, depth, last, i, n = [], 0, 0, 0, len(text)
    start = None
    while i < n:
        c = text[i]
        if c == "{":
            if depth == 0:
                header = text[last:i]
                m = re.findall(r"(\w+)\s*\(", header)
                start = (m[0] if m else None, i)
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                if start and start[0]:
                    res.append((start[0], start[1], i))
                last = i + 1
        elif c == ";" and depth == 0:
            last = i + 1
        i += 1
    return res


STORE = [
    re.compile(r"pCmdResp\s*\[[^\]]*\]\s*=(?!=)"),
    re.compile(r"memset\s*\(\s*(?:\(\s*\w+\s*\*\s*\)\s*)?dObj->pCmdResp"),
    re.compile(r"DRV_SDSPI_SPIRead\s*\(\s*dObj\s*,\s*(?:\(\s*\w+\s*\*\s*\)\s*)?dObj->pCmdResp"),
    re.compile(r"DRV_SDSPI_SPIWriteRead\s*\(\s*dObj\s*,[^,]+,\s*(?:\(\s*\w+\s*\*\s*\)\s*)?dObj->pCmdResp"),
]


def stores(text, lo, hi):
    pos = set()
    for rx in STORE:
        for m in rx.finditer(text, lo, hi):
            pos.add(m.start())
    return sorted(pos)


def guarded(text, pos, fstart):
    """Walk back from pos to the function start looking for the helper call at
    this block level or an enclosing one; a break or case label ends the walk."""
    depth = 0
    i = pos
    while i > fstart:
        i -= 1
        c = text[i]
        if c == "}":
            depth += 1
        elif c == "{":
            if depth > 0:
                depth -= 1
        elif depth == 0:
            if text.startswith(HELPER + "(", i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
                return True
            if text.startswith("break;", i) and not (text[i - 1].isalnum() or text[i - 1] == "_"):
                return False
            if re.match(r"case\s", text[i:i + 5]) and not (text[i - 1].isalnum() or text[i - 1] == "_"):
                return False
    return False


def check(src_text, iface_text=None):
    """Return a list of problems (empty = pass)."""
    text = strip(src_text)
    fns = functions(text)
    problems = []
    names = {f[0] for f in fns}
    if HELPER not in names:
        return ["helper %s is not defined" % HELPER]
    outside = set()
    for name, lo, hi in fns:
        sts = stores(text, lo, hi)
        if name == HELPER:
            body = text[lo:hi]
            first_scan = body.find("lDRV_SDSPI_WriteResyncScanReceived(dObj, true)")
            first_clear = body.find("memset")
            if first_scan < 0 or first_clear < 0 or first_scan > first_clear:
                problems.append("%s must scan inherited bytes BEFORE it clears" % HELPER)
            continue
        if name.startswith("lDRV_SDSPI_WriteResync"):
            for p in sts:
                if not guarded(text, p, lo):
                    line = text.count("\n", 0, p) + 1
                    problems.append("%s: unguarded write into pCmdResp at line %d (no %s before it in its block)"
                                    % (name, line, HELPER))
        elif sts:
            outside.add(name)
    if outside != OUTSIDE_ALLOWED:
        problems.append("writers of pCmdResp outside the resync path changed: %s (documented: %s)"
                        % (sorted(outside), sorted(OUTSIDE_ALLOWED)))
    # CommandSend: the divert to RESYNC_START precedes its first store.
    for name, lo, hi in fns:
        if name == "lDRV_SDSPI_CommandSend":
            body = text[lo:hi]
            gate = body.find("if (dObj->writeInDoubt != DRV_SDSPI_WRITE_IN_DOUBT_NONE)")
            first = min([p for p in stores(text, lo, hi)] or [10 ** 9]) - lo
            if gate < 0 or gate > first:
                problems.append("lDRV_SDSPI_CommandSend: the writeInDoubt divert must precede its first pCmdResp store")
    if iface_text is not None:
        t = strip(iface_text)
        for name, lo, hi in functions(t):
            if name == "DRV_SDSPI_AbortedXferRecord" and "resyncRxLate = true" not in t[lo:hi]:
                problems.append("DRV_SDSPI_AbortedXferRecord must mark the receive buffer late")
    return problems


def self_test():
    src = DEFAULT.read_text()
    iface = IFACE.read_text()
    base = check(src, iface)
    if base:
        print("self-test: the real source already fails:", base)
        return 1
    fails = 0

    def expect_fail(label, new, old, count_all=False, target=src):
        nonlocal fails
        if target.count(old) < 1:
            print("self-test %s: anchor missing" % label)
            fails += 1
            return
        mutated = target.replace(old, new) if count_all else target.replace(old, new, 1)
        got = check(mutated, iface) if target is src else check(src, mutated)
        if not got:
            print("self-test %s: NOT detected" % label)
            fails += 1
        else:
            print("self-test %s: detected" % label)

    # Each Reset call site removed in turn (every one must be needed).
    calls = [m.start() for m in re.finditer(re.escape(HELPER) + r"\(dObj,", src)]
    for k, p in enumerate(calls):
        end = src.index(";", p) + 1
        mutated = src[:p] + "(void) 0;" + src[end:]
        got = check(mutated, iface)
        label = "drop-reset-call-%d(line %d)" % (k, src.count("\n", 0, p) + 1)
        if not got:
            print("self-test %s: NOT detected" % label)
            fails += 1
        else:
            print("self-test %s: detected" % label)
    expect_fail("new-unguarded-write", "static void lDRV_SDSPI_WriteResyncBogus(DRV_SDSPI_OBJ* dObj)\n{\n    dObj->pCmdResp[0] = 1U;\n}\n\nstatic void lDRV_SDSPI_WriteResyncBusyRead(",
                "static void lDRV_SDSPI_WriteResyncBusyRead(")
    expect_fail("new-outside-writer", "static void lDRV_SDSPI_OtherWriter(DRV_SDSPI_OBJ* dObj)\n{\n    dObj->pCmdResp[2] = 1U;\n}\n\nstatic void lDRV_SDSPI_WriteResyncBusyRead(",
                "static void lDRV_SDSPI_WriteResyncBusyRead(")
    expect_fail("helper-clears-before-scan",
                "    (void) memset(dObj->pCmdResp, 0xFF, 1U);\n    lDRV_SDSPI_WriteResyncScanReceived(dObj, true);",
                "    lDRV_SDSPI_WriteResyncScanReceived(dObj, true);")
    expect_fail("aborted-record-not-late", "", "dObj->resyncRxLate = true;", target=iface)
    return 1 if fails else 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    src = DEFAULT
    if "--source" in sys.argv:
        src = Path(sys.argv[sys.argv.index("--source") + 1])
    iface = IFACE
    if "--iface" in sys.argv:
        iface = Path(sys.argv[sys.argv.index("--iface") + 1])
    problems = check(src.read_text(), iface.read_text() if iface.exists() else None)
    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    print("OK: every write into pCmdResp is guarded or documented (#1235 r5)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
